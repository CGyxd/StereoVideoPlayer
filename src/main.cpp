#include <windows.h>
#include <GL/glew.h>
#include <GL/wglew.h>
#include "FFmpegVideoReader.h"
#include <string>
#include <vector>
#include <iostream>
#include <filesystem>

// 窗口与 OpenGL 上下文句柄
static HWND g_hWnd = nullptr;
static HDC g_hDC = nullptr;
static HGLRC g_hRC = nullptr;

// 窗口尺寸（与视频分辨率一致）
static int g_WindowWidth = 0;
static int g_WindowHeight = 0;

// OpenGL 纹理与 PBO
static GLuint g_TextureID = 0;
static GLuint g_PBO = 0;

// 视频读取器
static FFmpegVideoReader* g_VideoReader = nullptr;
static bool g_Running = true;

// 窗口过程函数
LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
        {
            g_Running = false;
            PostQuitMessage(0);
        }
        break;
    case WM_CLOSE:
        g_Running = false;
        PostQuitMessage(0);
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }
    return 0;
}

// 设置支持 OpenGL Quad Buffer（立体）的像素格式
bool SetupPixelFormat(HDC hDC)
{
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER | PFD_STEREO;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    int pixelFormat = ChoosePixelFormat(hDC, &pfd);
    if (!pixelFormat || !SetPixelFormat(hDC, pixelFormat, &pfd))
    {
        return false;
    }
    return true;
}

// 创建窗口并初始化 OpenGL 上下文
bool InitWindow(HINSTANCE hInstance, int nCmdShow, int width, int height)
{
    WNDCLASSEX wcex = {};
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wcex.lpfnWndProc = WndProc;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wcex.lpszClassName = L"StereoVideoPlayerClass";

    if (!RegisterClassEx(&wcex))
    {
        return false;
    }

    RECT rect = { 0, 0, width, height };
    AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_APPWINDOW);

    g_hWnd = CreateWindowEx(
        WS_EX_APPWINDOW,
        L"StereoVideoPlayerClass",
        L"Stereo Video Player",
        WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rect.right - rect.left, rect.bottom - rect.top,
        nullptr, nullptr, hInstance, nullptr);

    if (!g_hWnd)
    {
        return false;
    }

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    g_hDC = GetDC(g_hWnd);
    if (!SetupPixelFormat(g_hDC))
    {
        return false;
    }

    g_hRC = wglCreateContext(g_hDC);
    if (!g_hRC)
    {
        return false;
    }

    wglMakeCurrent(g_hDC, g_hRC);

    GLenum err = glewInit();
    if (err != GLEW_OK)
    {
        return false;
    }

    // 关闭垂直同步，避免帧率受限
    if (WGLEW_EXT_swap_control)
    {
        wglSwapIntervalEXT(0);
    }

    g_WindowWidth = width;
    g_WindowHeight = height;

    return true;
}

// 初始化纹理与 PBO
bool InitGL()
{
    glGenTextures(1, &g_TextureID);
    glBindTexture(GL_TEXTURE_2D, g_TextureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // 初始化为黑色
    std::vector<byte_t> black(g_WindowWidth * g_WindowHeight * 4, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_WindowWidth, g_WindowHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, black.data());

    glGenBuffers(1, &g_PBO);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, g_PBO);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, g_WindowWidth * g_WindowHeight * 4, nullptr, GL_STREAM_DRAW);

    glEnable(GL_TEXTURE_2D);

    return true;
}

// 绘制覆盖全屏的四边形，offset 控制纹理水平采样区域
void RenderTexture(float offset)
{
    glBegin(GL_QUADS);
    glTexCoord2f(offset + 0.5f, 0.0f); glVertex2f(1.0f, 1.0f);
    glTexCoord2f(offset, 0.0f);        glVertex2f(-1.0f, 1.0f);
    glTexCoord2f(offset, 1.0f);        glVertex2f(-1.0f, -1.0f);
    glTexCoord2f(offset + 0.5f, 1.0f); glVertex2f(1.0f, -1.0f);
    glEnd();
}

// 将 BGRA 帧数据上传到 OpenGL 纹理
void UpdateTexture(const std::vector<byte_t>& frameData)
{
    if (frameData.size() < static_cast<size_t>(g_WindowWidth * g_WindowHeight * 4))
    {
        return;
    }

    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, g_PBO);
    void* ptr = glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);
    if (ptr)
    {
        memcpy(ptr, frameData.data(), g_WindowWidth * g_WindowHeight * 4);
        glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    }

    glBindTexture(GL_TEXTURE_2D, g_TextureID);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, g_WindowWidth, g_WindowHeight, GL_BGRA, GL_UNSIGNED_BYTE, 0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
}

static std::string WideToUtf8(const std::wstring& wstr)
{
    if (wstr.empty()) return std::string();

    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &result[0], size, nullptr, nullptr);
    if (!result.empty() && result.back() == '\0')
    {
        result.pop_back();
    }
    return result;
}

// 获取当前可执行文件所在目录，默认视频与 exe 同级
std::string GetDefaultVideoPath()
{
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileName(nullptr, exePath, MAX_PATH);

    std::wstring path(exePath);
    size_t lastSlash = path.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos)
    {
        path = path.substr(0, lastSlash);
    }

    if (!path.empty() && path.back() != L'\\' && path.back() != L'/')
    {
        path += L'\\';
    }
    return WideToUtf8(path + L"左右立体片源.mkv");
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR lpCmdLine, int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);

    // 默认视频路径为与 exe 同级的 左右立体片源.mkv
    std::string videoPath = GetDefaultVideoPath();

    // 如果命令行传入路径则使用传入路径，并去除可能存在的引号
    if (lpCmdLine && wcslen(lpCmdLine) > 0)
    {
        std::wstring wcmd = lpCmdLine;
        if (!wcmd.empty() && wcmd.front() == L'\"')
        {
            wcmd = wcmd.substr(1, wcmd.size() - 1);
        }
        if (!wcmd.empty() && wcmd.back() == L'\"')
        {
            wcmd.pop_back();
        }
        videoPath = WideToUtf8(wcmd);
    }

    if (!std::filesystem::exists(std::filesystem::u8path(videoPath)))
    {
        std::wstring msg = L"找不到视频文件：\n";
        // 简单将 UTF-8 字节按宽字符显示，主要用于提示
        msg += std::wstring(videoPath.begin(), videoPath.end());
        msg += L"\n\n请确认 左右立体片源.mkv 与 exe 放在同一目录，或通过命令行传入完整路径。";
        MessageBox(nullptr, msg.c_str(), L"错误", MB_OK | MB_ICONERROR);
        return 1;
    }

    g_VideoReader = new FFmpegVideoReader();
    if (!g_VideoReader->Open(videoPath))
    {
        MessageBox(nullptr, L"无法打开视频文件，请确认 左右立体片源.mkv 与 exe 放在同一目录，且文件未损坏。", L"错误", MB_OK | MB_ICONERROR);
        delete g_VideoReader;
        g_VideoReader = nullptr;
        return 1;
    }

    int videoWidth = g_VideoReader->GetWidth();
    int videoHeight = g_VideoReader->GetHeight();
    if (videoWidth <= 0 || videoHeight <= 0)
    {
        // 无法解析视频尺寸，使用 1920x1080 作为默认尺寸
        videoWidth = 1920;
        videoHeight = 1080;
    }

    if (!InitWindow(hInstance, nCmdShow, videoWidth, videoHeight))
    {
        MessageBox(nullptr, L"初始化 OpenGL 窗口失败，请检查显卡是否支持 Quad Buffer 立体显示", L"错误", MB_OK | MB_ICONERROR);
        delete g_VideoReader;
        g_VideoReader = nullptr;
        return 1;
    }

    if (!InitGL())
    {
        MessageBox(nullptr, L"初始化 OpenGL 资源失败", L"错误", MB_OK | MB_ICONERROR);
        delete g_VideoReader;
        g_VideoReader = nullptr;
        return 1;
    }

    std::vector<byte_t> frameData;
    MSG msg = {};
    double fps = g_VideoReader->GetFrameRate();
    if (fps <= 0) fps = 30.0;

    while (g_Running)
    {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        if (!g_Running)
        {
            break;
        }

        int w = 0;
        int h = 0;
        if (!g_VideoReader->ReadFrame(frameData, w, h))
        {
            // 到达末尾则重新从头播放
            if (g_VideoReader->IsEndOfStream())
            {
                g_VideoReader->Close();
                g_VideoReader->Open(videoPath);
            }
            continue;
        }

        UpdateTexture(frameData);

        // 分别渲染到左、右后缓冲
        glDrawBuffer(GL_BACK_LEFT);
        RenderTexture(0.0f);

        glDrawBuffer(GL_BACK_RIGHT);
        RenderTexture(0.5f);

        SwapBuffers(g_hDC);

        // 按视频帧率简单限速
        Sleep(static_cast<DWORD>(1000.0 / fps));
    }

    // 清理资源
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(g_hRC);
    ReleaseDC(g_hWnd, g_hDC);
    DestroyWindow(g_hWnd);

    delete g_VideoReader;
    g_VideoReader = nullptr;

    return 0;
}
