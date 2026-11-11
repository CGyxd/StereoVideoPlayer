// Linux 版本：基于 X11 + GLX 实现 OpenGL Quad Buffer 立体视频播放
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <GL/glew.h>
#include <GL/glx.h>
#include <GL/gl.h>

#include "FFmpegVideoReader.h"

#include <string>
#include <vector>
#include <iostream>
#include <filesystem>
#include <thread>
#include <chrono>
#include <cstring>
#include <unistd.h>

// X11 显示与窗口
static Display* g_Dpy = nullptr;
static Window g_Win = 0;
static GLXContext g_Ctx = nullptr;

// 窗口关闭协议原子
static Atom g_WMDeleteMessage = 0;
static Atom g_WMProtocols = 0;

static int g_WindowWidth = 0;
static int g_WindowHeight = 0;

static GLuint g_TextureID = 0;
static GLuint g_PBO = 0;

static FFmpegVideoReader* g_VideoReader = nullptr;

// 是否启用 Quad Buffer 立体输出
static bool g_StereoEnabled = false;

// 获取当前可执行文件所在目录，默认视频与可执行文件同级
static std::string GetExecutableDirectory()
{
    char buffer[4096] = {};
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len <= 0)
    {
        return std::string();
    }
    buffer[len] = '\0';

    std::string path(buffer);
    size_t lastSlash = path.find_last_of("/");
    if (lastSlash != std::string::npos)
    {
        path = path.substr(0, lastSlash);
    }
    if (!path.empty() && path.back() != '/')
    {
        path += '/';
    }
    return path;
}

// 绘制覆盖全屏的四边形，offset 控制纹理水平采样区域，uRange 控制水平采样宽度
void RenderTexture(float offset, float uRange)
{
    glBegin(GL_QUADS);
    glTexCoord2f(offset + uRange, 0.0f); glVertex2f(1.0f, 1.0f);
    glTexCoord2f(offset, 0.0f);          glVertex2f(-1.0f, 1.0f);
    glTexCoord2f(offset, 1.0f);          glVertex2f(-1.0f, -1.0f);
    glTexCoord2f(offset + uRange, 1.0f); glVertex2f(1.0f, -1.0f);
    glEnd();
}

// 渲染左眼或右眼画面
void RenderEye(float offset)
{
    RenderTexture(offset, 0.5f);
}

// 初始化 OpenGL 纹理与 PBO
bool InitGL()
{
    glGenTextures(1, &g_TextureID);
    glBindTexture(GL_TEXTURE_2D, g_TextureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    std::vector<byte_t> black(g_WindowWidth * g_WindowHeight * 4, 0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_WindowWidth, g_WindowHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, black.data());

    glGenBuffers(1, &g_PBO);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, g_PBO);
    glBufferData(GL_PIXEL_UNPACK_BUFFER, g_WindowWidth * g_WindowHeight * 4, nullptr, GL_STREAM_DRAW);

    glEnable(GL_TEXTURE_2D);
    return true;
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

// 创建 X11 窗口并建立支持 GLX_STEREO 的 OpenGL 上下文
bool InitWindowX11(int width, int height)
{
    g_Dpy = XOpenDisplay(nullptr);
    if (!g_Dpy)
    {
        std::cerr << "无法打开 X11 显示" << std::endl;
        return false;
    }

    // 注册窗口关闭协议
    g_WMDeleteMessage = XInternAtom(g_Dpy, "WM_DELETE_WINDOW", False);
    g_WMProtocols = XInternAtom(g_Dpy, "WM_PROTOCOLS", False);

    int screen = DefaultScreen(g_Dpy);
    Window root = RootWindow(g_Dpy, screen);

    int stereoVisualAtt[] = {
        GLX_RGBA,
        GLX_DOUBLEBUFFER,
        GLX_STEREO,
        GLX_RED_SIZE, 8,
        GLX_GREEN_SIZE, 8,
        GLX_BLUE_SIZE, 8,
        GLX_DEPTH_SIZE, 24,
        None
    };

    int monoVisualAtt[] = {
        GLX_RGBA,
        GLX_DOUBLEBUFFER,
        GLX_RED_SIZE, 8,
        GLX_GREEN_SIZE, 8,
        GLX_BLUE_SIZE, 8,
        GLX_DEPTH_SIZE, 24,
        None
    };

    // 优先尝试 Quad Buffer 立体 visual，不支持则回退到普通双缓冲
    XVisualInfo* vi = glXChooseVisual(g_Dpy, screen, stereoVisualAtt);
    if (vi)
    {
        g_StereoEnabled = true;
        std::cout << "已启用 GLX_STEREO 立体输出" << std::endl;
    }
    else
    {
        std::cerr << "当前显卡/驱动不支持 GLX_STEREO，已回退到普通双缓冲" << std::endl;
        vi = glXChooseVisual(g_Dpy, screen, monoVisualAtt);
        if (!vi)
        {
            std::cerr << "无法找到可用的 GLX visual" << std::endl;
            XCloseDisplay(g_Dpy);
            g_Dpy = nullptr;
            return false;
        }
        g_StereoEnabled = false;
    }

    Colormap cmap = XCreateColormap(g_Dpy, root, vi->visual, AllocNone);

    XSetWindowAttributes swa = {};
    swa.colormap = cmap;
    swa.event_mask = ExposureMask | KeyPressMask | StructureNotifyMask;

    g_Win = XCreateWindow(
        g_Dpy, root,
        0, 0, width, height,
        0,
        vi->depth,
        InputOutput,
        vi->visual,
        CWColormap | CWEventMask,
        &swa);

    XMapWindow(g_Dpy, g_Win);
    XStoreName(g_Dpy, g_Win, "Stereo Video Player");

    // 告知窗口管理器：支持 WM_DELETE_WINDOW 关闭协议
    XSetWMProtocols(g_Dpy, g_Win, &g_WMDeleteMessage, 1);

    g_Ctx = glXCreateContext(g_Dpy, vi, nullptr, GL_TRUE);
    if (!g_Ctx)
    {
        std::cerr << "无法创建 GLX 上下文" << std::endl;
        XDestroyWindow(g_Dpy, g_Win);
        XCloseDisplay(g_Dpy);
        g_Dpy = nullptr;
        g_Win = 0;
        return false;
    }

    glXMakeCurrent(g_Dpy, g_Win, g_Ctx);

    GLenum err = glewInit();
    if (err != GLEW_OK)
    {
        std::cerr << "GLEW 初始化失败" << std::endl;
        glXDestroyContext(g_Dpy, g_Ctx);
        XDestroyWindow(g_Dpy, g_Win);
        XCloseDisplay(g_Dpy);
        g_Dpy = nullptr;
        g_Win = 0;
        g_Ctx = nullptr;
        return false;
    }

    g_WindowWidth = width;
    g_WindowHeight = height;

    XFree(vi);
    return true;
}

// 清理 X11 / GLX 资源
void CleanupX11()
{
    if (g_Ctx)
    {
        glXMakeCurrent(g_Dpy, None, nullptr);
        glXDestroyContext(g_Dpy, g_Ctx);
        g_Ctx = nullptr;
    }
    if (g_Win && g_Dpy)
    {
        XDestroyWindow(g_Dpy, g_Win);
        g_Win = 0;
    }
    if (g_Dpy)
    {
        XCloseDisplay(g_Dpy);
        g_Dpy = nullptr;
    }
}

int main(int argc, char** argv)
{
    // 默认与可执行文件同目录
    std::string videoPath = GetExecutableDirectory() + "左右立体片源.mkv";
    if (argc > 1)
    {
        videoPath = argv[1];
    }

    if (!std::filesystem::exists(videoPath))
    {
        std::cerr << "找不到视频文件：" << videoPath << std::endl;
        std::cerr << "请确认 左右立体片源.mkv 与可执行文件在同一目录，或通过命令行传入路径。" << std::endl;
        return 1;
    }

    g_VideoReader = new FFmpegVideoReader();
    if (!g_VideoReader->Open(videoPath))
    {
        std::cerr << "无法打开视频文件：" << videoPath << std::endl;
        delete g_VideoReader;
        g_VideoReader = nullptr;
        return 1;
    }

    int videoWidth = g_VideoReader->GetWidth();
    int videoHeight = g_VideoReader->GetHeight();
    if (videoWidth <= 0 || videoHeight <= 0)
    {
        videoWidth = 1920;
        videoHeight = 1080;
    }

    if (!InitWindowX11(videoWidth, videoHeight))
    {
        delete g_VideoReader;
        g_VideoReader = nullptr;
        return 1;
    }

    if (!InitGL())
    {
        std::cerr << "初始化 OpenGL 资源失败" << std::endl;
        CleanupX11();
        delete g_VideoReader;
        g_VideoReader = nullptr;
        return 1;
    }

    std::vector<byte_t> frameData;
    double fps = g_VideoReader->GetFrameRate();
    if (fps <= 0) fps = 30.0;

    bool running = true;
    XEvent xev;

    while (running)
    {
        // 处理 X11 事件
        while (XPending(g_Dpy) > 0)
        {
            XNextEvent(g_Dpy, &xev);
            if (xev.type == KeyPress)
            {
                KeySym key = XLookupKeysym(&xev.xkey, 0);
                if (key == XK_Escape)
                {
                    running = false;
                }
            }
            else if (xev.type == ClientMessage)
            {
                // 仅当收到 WM_DELETE_WINDOW 时才关闭窗口
                if (static_cast<Atom>(xev.xclient.message_type) == g_WMProtocols &&
                    static_cast<Atom>(xev.xclient.data.l[0]) == g_WMDeleteMessage)
                {
                    running = false;
                }
            }
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

        // 无论当前是否启用 Quad Buffer，都分别绘制左、右眼画面
        // 若显卡支持 GLX_STEREO，则分别写入左、右后缓冲；
        // 若不支持，GL_BACK_LEFT / GL_BACK_RIGHT 通常等价于 GL_BACK，
        // 后绘制的右眼会覆盖左眼，行为与 Windows 版本保持一致。
        glDrawBuffer(GL_BACK_LEFT);
        RenderEye(0.0f);

        glDrawBuffer(GL_BACK_RIGHT);
        RenderEye(0.5f);

        glXSwapBuffers(g_Dpy, g_Win);

        // 按视频帧率限速
        std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(1000.0 / fps));
    }

    CleanupX11();
    delete g_VideoReader;
    g_VideoReader = nullptr;

    return 0;
}
