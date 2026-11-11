#include "FFmpegVideoReader.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <iostream>

FFmpegVideoReader::FFmpegVideoReader()
    : m_pFormatCtx(nullptr)
    , m_pCodecCtx(nullptr)
    , m_pFrame(nullptr)
    , m_pFrameRGB(nullptr)
    , m_pSwsCtx(nullptr)
    , m_VideoStreamIndex(-1)
    , m_Width(0)
    , m_Height(0)
    , m_FrameRate(0.0)
    , m_bEndOfStream(false)
{
    // 初始化 FFmpeg 网络库（即使是本地文件也调用一次无害）
    avformat_network_init();
}

FFmpegVideoReader::~FFmpegVideoReader()
{
    Close();
    avformat_network_deinit();
}

bool FFmpegVideoReader::Open(const std::string& filePath)
{
    Close();

    // 打开输入文件
    int ret = avformat_open_input(&m_pFormatCtx, filePath.c_str(), nullptr, nullptr);
    if (ret < 0)
    {
        return false;
    }

    // 读取流信息
    ret = avformat_find_stream_info(m_pFormatCtx, nullptr);
    if (ret < 0)
    {
        Close();
        return false;
    }

    // 查找第一个视频流
    for (unsigned int i = 0; i < m_pFormatCtx->nb_streams; ++i)
    {
        AVCodecParameters* pCodecParams = m_pFormatCtx->streams[i]->codecpar;
        if (pCodecParams->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            m_VideoStreamIndex = static_cast<int>(i);
            break;
        }
    }

    if (m_VideoStreamIndex == -1)
    {
        Close();
        return false;
    }

    AVCodecParameters* pCodecParams = m_pFormatCtx->streams[m_VideoStreamIndex]->codecpar;

    // 查找解码器
    const AVCodec* pCodec = avcodec_find_decoder(pCodecParams->codec_id);
    if (!pCodec)
    {
        Close();
        return false;
    }

    // 创建解码器上下文
    m_pCodecCtx = avcodec_alloc_context3(pCodec);
    if (!m_pCodecCtx)
    {
        Close();
        return false;
    }

    ret = avcodec_parameters_to_context(m_pCodecCtx, pCodecParams);
    if (ret < 0)
    {
        Close();
        return false;
    }

    // 多线程解码，提升性能
    m_pCodecCtx->thread_count = 4;

    ret = avcodec_open2(m_pCodecCtx, pCodec, nullptr);
    if (ret < 0)
    {
        Close();
        return false;
    }

    m_Width = m_pCodecCtx->width;
    m_Height = m_pCodecCtx->height;

    // 读取帧率，优先 avg_frame_rate，否则使用 r_frame_rate
    AVStream* pStream = m_pFormatCtx->streams[m_VideoStreamIndex];
    m_FrameRate = av_q2d(pStream->avg_frame_rate);
    if (m_FrameRate <= 0 || m_FrameRate > 240.0)
    {
        m_FrameRate = av_q2d(pStream->r_frame_rate);
    }
    if (m_FrameRate <= 0 || m_FrameRate > 240.0)
    {
        m_FrameRate = 30.0;
    }

    // 分配帧
    m_pFrame = av_frame_alloc();
    m_pFrameRGB = av_frame_alloc();
    if (!m_pFrame || !m_pFrameRGB)
    {
        Close();
        return false;
    }

    // 创建图像缩放/格式转换上下文，输出 BGRA
    m_pSwsCtx = sws_getContext(
        m_Width, m_Height, m_pCodecCtx->pix_fmt,
        m_Width, m_Height, AV_PIX_FMT_BGRA,
        SWS_BILINEAR, nullptr, nullptr, nullptr);

    if (!m_pSwsCtx)
    {
        Close();
        return false;
    }

    return true;
}

bool FFmpegVideoReader::ReadFrame(std::vector<byte_t>& outFrame, int& width, int& height)
{
    if (!m_pFormatCtx || !m_pCodecCtx || m_bEndOfStream)
    {
        return false;
    }

    AVPacket packet;
    av_init_packet(&packet);
    packet.data = nullptr;
    packet.size = 0;

    while (true)
    {
        int ret = av_read_frame(m_pFormatCtx, &packet);
        if (ret < 0)
        {
            // 发送 flush packet，尝试取出缓冲帧
            avcodec_send_packet(m_pCodecCtx, nullptr);
            ret = avcodec_receive_frame(m_pCodecCtx, m_pFrame);
            if (ret >= 0)
            {
                // 转换并输出
                int numBytes = av_image_get_buffer_size(AV_PIX_FMT_BGRA, m_Width, m_Height, 1);
                outFrame.resize(numBytes);
                uint8_t* pData[4] = { outFrame.data(), nullptr, nullptr, nullptr };
                int linesize[4] = { m_Width * 4, 0, 0, 0 };
                sws_scale(m_pSwsCtx, m_pFrame->data, m_pFrame->linesize, 0, m_Height, pData, linesize);

                width = m_Width;
                height = m_Height;

                av_packet_unref(&packet);
                return true;
            }

            m_bEndOfStream = true;
            av_packet_unref(&packet);
            return false;
        }

        if (packet.stream_index == m_VideoStreamIndex)
        {
            ret = avcodec_send_packet(m_pCodecCtx, &packet);
            if (ret < 0)
            {
                av_packet_unref(&packet);
                continue;
            }

            ret = avcodec_receive_frame(m_pCodecCtx, m_pFrame);
            av_packet_unref(&packet);

            if (ret == AVERROR(EAGAIN))
            {
                // 需要更多 packet
                continue;
            }
            if (ret == AVERROR_EOF)
            {
                m_bEndOfStream = true;
                return false;
            }
            if (ret < 0)
            {
                return false;
            }

            // 成功收到一帧，转换为 BGRA
            int numBytes = av_image_get_buffer_size(AV_PIX_FMT_BGRA, m_Width, m_Height, 1);
            outFrame.resize(numBytes);
            uint8_t* pData[4] = { outFrame.data(), nullptr, nullptr, nullptr };
            int linesize[4] = { m_Width * 4, 0, 0, 0 };
            sws_scale(m_pSwsCtx, m_pFrame->data, m_pFrame->linesize, 0, m_Height, pData, linesize);

            width = m_Width;
            height = m_Height;
            return true;
        }

        av_packet_unref(&packet);
    }
}

void FFmpegVideoReader::Close()
{
    if (m_pSwsCtx)
    {
        sws_freeContext(m_pSwsCtx);
        m_pSwsCtx = nullptr;
    }
    if (m_pFrameRGB)
    {
        av_frame_free(&m_pFrameRGB);
    }
    if (m_pFrame)
    {
        av_frame_free(&m_pFrame);
    }
    if (m_pCodecCtx)
    {
        avcodec_free_context(&m_pCodecCtx);
    }
    if (m_pFormatCtx)
    {
        avformat_close_input(&m_pFormatCtx);
    }

    m_VideoStreamIndex = -1;
    m_Width = 0;
    m_Height = 0;
    m_FrameRate = 0.0;
    m_bEndOfStream = false;
}
