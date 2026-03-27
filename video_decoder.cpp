#include "video_decoder.h"

#ifdef HAVE_FFMPEG

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
}

#include <iostream>
#include <chrono>

struct VideoDecoder::FFmpegContext {
    AVFormatContext* formatCtx = nullptr;
    AVCodecContext* codecCtx = nullptr;
    SwsContext* swsCtx = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* rgbaFrame = nullptr;
    AVPacket* packet = nullptr;
    int videoStreamIndex = -1;
    uint8_t* rgbaBuffer = nullptr;

    ~FFmpegContext() {
        if (rgbaBuffer) av_free(rgbaBuffer);
        if (rgbaFrame) av_frame_free(&rgbaFrame);
        if (frame) av_frame_free(&frame);
        if (packet) av_packet_free(&packet);
        if (swsCtx) sws_freeContext(swsCtx);
        if (codecCtx) avcodec_free_context(&codecCtx);
        if (formatCtx) avformat_close_input(&formatCtx);
    }
};

VideoDecoder::VideoDecoder() {}

VideoDecoder::~VideoDecoder() {
    stop();
    close();
}

bool VideoDecoder::open(const std::string& source) {
    close();

    m_source = source;
    m_ff = std::make_unique<FFmpegContext>();

    // Set options for network streams
    AVDictionary* opts = nullptr;
    bool isStream = source.find("://") != std::string::npos;
    if (isStream) {
        av_dict_set(&opts, "rtsp_transport", "tcp", 0);
        av_dict_set(&opts, "stimeout", "5000000", 0);   // 5s timeout for RTSP
        av_dict_set(&opts, "timeout", "5000000", 0);     // general timeout
        av_dict_set(&opts, "reconnect", "1", 0);
        av_dict_set(&opts, "reconnect_streamed", "1", 0);
        av_dict_set(&opts, "reconnect_delay_max", "5", 0);
    }

    int ret = avformat_open_input(&m_ff->formatCtx, source.c_str(), nullptr, &opts);
    if (opts) av_dict_free(&opts);

    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "Failed to open video source: " << errbuf << std::endl;
        m_ff.reset();
        return false;
    }

    if (avformat_find_stream_info(m_ff->formatCtx, nullptr) < 0) {
        std::cerr << "Failed to find stream info" << std::endl;
        m_ff.reset();
        return false;
    }

    // Find best video stream
    m_ff->videoStreamIndex = av_find_best_stream(
        m_ff->formatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (m_ff->videoStreamIndex < 0) {
        std::cerr << "No video stream found in source" << std::endl;
        m_ff.reset();
        return false;
    }

    AVStream* stream = m_ff->formatCtx->streams[m_ff->videoStreamIndex];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        std::cerr << "Unsupported codec: " << avcodec_get_name(stream->codecpar->codec_id) << std::endl;
        m_ff.reset();
        return false;
    }

    m_ff->codecCtx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(m_ff->codecCtx, stream->codecpar);

    // Enable multi-threaded decoding
    m_ff->codecCtx->thread_count = 0; // auto
    m_ff->codecCtx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;

    if (avcodec_open2(m_ff->codecCtx, codec, nullptr) < 0) {
        std::cerr << "Failed to open codec" << std::endl;
        m_ff.reset();
        return false;
    }

    // Allocate frames and packet
    m_ff->frame = av_frame_alloc();
    m_ff->rgbaFrame = av_frame_alloc();
    m_ff->packet = av_packet_alloc();

    // Set up RGBA output frame
    int w = m_ff->codecCtx->width;
    int h = m_ff->codecCtx->height;
    int bufSize = av_image_get_buffer_size(AV_PIX_FMT_RGBA, w, h, 1);
    m_ff->rgbaBuffer = (uint8_t*)av_malloc(bufSize);
    av_image_fill_arrays(m_ff->rgbaFrame->data, m_ff->rgbaFrame->linesize,
                         m_ff->rgbaBuffer, AV_PIX_FMT_RGBA, w, h, 1);

    // Create scaler context
    m_ff->swsCtx = sws_getContext(
        w, h, m_ff->codecCtx->pix_fmt,
        w, h, AV_PIX_FMT_RGBA,
        SWS_BILINEAR, nullptr, nullptr, nullptr);

    if (!m_ff->swsCtx) {
        std::cerr << "Failed to create scaler context" << std::endl;
        m_ff.reset();
        return false;
    }

    auto info = getSourceInfo();
    std::cout << "Opened video: " << info.source
              << " (" << info.width << "x" << info.height
              << " @ " << info.fps << " fps"
              << ", codec: " << info.codec;
    if (info.duration > 0)
        std::cout << ", duration: " << info.duration << "s";
    else
        std::cout << ", live stream";
    std::cout << ")" << std::endl;

    return true;
}

void VideoDecoder::close() {
    stop();
    m_ff.reset();
    m_source.clear();
    m_active = false;
}

void VideoDecoder::start() {
    if (!m_ff || m_running) return;

    m_running = true;
    m_active = true;
    m_thread = std::thread(&VideoDecoder::decoderLoop, this);
}

void VideoDecoder::stop() {
    m_running = false;
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

bool VideoDecoder::getLatestFrame(Texture& outTexture) {
    std::lock_guard<std::mutex> lock(m_frameMutex);
    if (m_newFrameAvailable && m_currentFrame.isValid()) {
        outTexture = m_currentFrame;
        m_newFrameAvailable = false;
        return true;
    }
    return false;
}

bool VideoDecoder::isActive() const {
    return m_active;
}

VideoDecoder::SourceInfo VideoDecoder::getSourceInfo() const {
    SourceInfo info;
    info.source = m_source;
    if (!m_ff || !m_ff->codecCtx) return info;

    info.width = m_ff->codecCtx->width;
    info.height = m_ff->codecCtx->height;
    info.codec = avcodec_get_name(m_ff->codecCtx->codec_id);

    AVStream* stream = m_ff->formatCtx->streams[m_ff->videoStreamIndex];
    if (stream->avg_frame_rate.den > 0) {
        info.fps = av_q2d(stream->avg_frame_rate);
    } else if (stream->r_frame_rate.den > 0) {
        info.fps = av_q2d(stream->r_frame_rate);
    }

    if (m_ff->formatCtx->duration > 0) {
        info.duration = (double)m_ff->formatCtx->duration / AV_TIME_BASE;
    }

    return info;
}

void VideoDecoder::decoderLoop() {
    if (!m_ff) return;

    int w = m_ff->codecCtx->width;
    int h = m_ff->codecCtx->height;
    int rgbaSize = w * h * 4;

    // Calculate frame duration for pacing
    AVStream* stream = m_ff->formatCtx->streams[m_ff->videoStreamIndex];
    double fps = 30.0;
    if (stream->avg_frame_rate.den > 0)
        fps = av_q2d(stream->avg_frame_rate);
    else if (stream->r_frame_rate.den > 0)
        fps = av_q2d(stream->r_frame_rate);

    auto frameDuration = std::chrono::microseconds((int64_t)(1000000.0 / fps));
    bool isStream = m_source.find("://") != std::string::npos;

    while (m_running) {
        auto frameStart = std::chrono::steady_clock::now();

        int ret = av_read_frame(m_ff->formatCtx, m_ff->packet);
        if (ret < 0) {
            if (ret == AVERROR_EOF || ret == AVERROR(EIO)) {
                if (!isStream && m_loop) {
                    // Seek back to beginning for file looping
                    av_seek_frame(m_ff->formatCtx, m_ff->videoStreamIndex, 0, AVSEEK_FLAG_BACKWARD);
                    avcodec_flush_buffers(m_ff->codecCtx);
                    continue;
                }
                // End of stream or file (no loop)
                m_active = false;
                break;
            }
            // Transient error, retry
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        if (m_ff->packet->stream_index != m_ff->videoStreamIndex) {
            av_packet_unref(m_ff->packet);
            continue;
        }

        ret = avcodec_send_packet(m_ff->codecCtx, m_ff->packet);
        av_packet_unref(m_ff->packet);
        if (ret < 0) continue;

        while (avcodec_receive_frame(m_ff->codecCtx, m_ff->frame) == 0) {
            // Convert to RGBA
            sws_scale(m_ff->swsCtx,
                      m_ff->frame->data, m_ff->frame->linesize,
                      0, h,
                      m_ff->rgbaFrame->data, m_ff->rgbaFrame->linesize);

            // Copy to texture
            std::vector<unsigned char> pixels(rgbaSize);
            // Handle stride: rgbaFrame linesize may differ from w*4
            int dstStride = w * 4;
            if (m_ff->rgbaFrame->linesize[0] == dstStride) {
                memcpy(pixels.data(), m_ff->rgbaFrame->data[0], rgbaSize);
            } else {
                for (int y = 0; y < h; y++) {
                    memcpy(pixels.data() + y * dstStride,
                           m_ff->rgbaFrame->data[0] + y * m_ff->rgbaFrame->linesize[0],
                           dstStride);
                }
            }

            {
                std::lock_guard<std::mutex> lock(m_frameMutex);
                m_currentFrame.setOwnedPixels(std::move(pixels), w, h, 4, ColorFormat::RGBA);
                m_newFrameAvailable = true;
            }
        }

        // Pace for file playback (streams pace naturally via network)
        if (!isStream) {
            auto elapsed = std::chrono::steady_clock::now() - frameStart;
            auto remaining = frameDuration - elapsed;
            if (remaining.count() > 0) {
                std::this_thread::sleep_for(remaining);
            }
        }
    }
}

#endif // HAVE_FFMPEG
