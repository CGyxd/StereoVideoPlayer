#pragma once

#include <string>
#include <vector>

// 跨平台字节类型别名
using byte_t = unsigned char;

// 基于 FFmpeg 4.2 的简单视频帧读取器
class FFmpegVideoReader
{
public:
    FFmpegVideoReader();
    ~FFmpegVideoReader();

    // 打开指定路径的视频文件，路径使用 UTF-8 编码
    bool Open(const std::string& filePath);

    // 读取下一帧，输出为 BGRA 32 位数据（width * height * 4）
    bool ReadFrame(std::vector<byte_t>& outFrame, int& width, int& height);

    // 关闭并释放资源
    void Close();

    // 获取视频帧率
    double GetFrameRate() const { return m_FrameRate; }

    // 获取视频宽高（打开后有效）
    int GetWidth() const { return m_Width; }
    int GetHeight() const { return m_Height; }

    // 是否已经到达文件末尾
    bool IsEndOfStream() const { return m_bEndOfStream; }

private:
    struct AVFormatContext* m_pFormatCtx;
    struct AVCodecContext* m_pCodecCtx;
    struct AVFrame* m_pFrame;
    struct AVFrame* m_pFrameRGB;
    struct SwsContext* m_pSwsCtx;
    int m_VideoStreamIndex;
    int m_Width;
    int m_Height;
    double m_FrameRate;
    bool m_bEndOfStream;
};
