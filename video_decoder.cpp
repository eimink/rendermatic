#include "video_decoder.h"

#ifdef HAVE_FFMPEG

extern "C" {
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libavutil/hwcontext.h>
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
    int lastPixFmt = -1;
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

// Resolve HLS master playlist to the highest bandwidth variant URL.
// Returns the resolved URL, or the original if not an HLS master playlist.
static std::string resolveHlsVariant(const std::string& url) {
    if (url.find(".m3u8") == std::string::npos)
        return url;

    // Open and read the playlist
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "timeout", "5000000", 0);
    AVIOContext* io = nullptr;
    if (avio_open2(&io, url.c_str(), AVIO_FLAG_READ, nullptr, &opts) < 0) {
        if (opts) av_dict_free(&opts);
        return url;
    }
    if (opts) av_dict_free(&opts);

    // Read up to 64KB of the playlist
    char buf[65536];
    int len = avio_read(io, (unsigned char*)buf, sizeof(buf) - 1);
    avio_closep(&io);
    if (len <= 0) return url;
    buf[len] = '\0';

    // Parse: find highest BANDWIDTH in EXT-X-STREAM-INF lines
    std::string bestUrl;
    int64_t bestBw = -1;
    std::string playlist(buf, len);
    size_t pos = 0;
    while ((pos = playlist.find("#EXT-X-STREAM-INF:", pos)) != std::string::npos) {
        size_t lineEnd = playlist.find('\n', pos);
        if (lineEnd == std::string::npos) break;
        std::string line = playlist.substr(pos, lineEnd - pos);

        int64_t bw = 0;
        size_t bwPos = line.find("BANDWIDTH=");
        if (bwPos != std::string::npos)
            bw = std::strtoll(line.c_str() + bwPos + 10, nullptr, 10);

        // Next non-empty, non-comment line is the variant URL
        size_t urlStart = lineEnd + 1;
        while (urlStart < playlist.size() && (playlist[urlStart] == '\n' || playlist[urlStart] == '\r' || playlist[urlStart] == '#')) {
            if (playlist[urlStart] == '#')
                urlStart = playlist.find('\n', urlStart);
            if (urlStart == std::string::npos) break;
            urlStart++;
        }
        if (urlStart >= playlist.size()) break;
        size_t urlEnd = playlist.find_first_of("\r\n", urlStart);
        std::string variantUrl = playlist.substr(urlStart, urlEnd - urlStart);

        if (bw > bestBw) {
            bestBw = bw;
            // Handle relative URLs
            if (variantUrl.find("://") == std::string::npos) {
                size_t lastSlash = url.rfind('/');
                if (lastSlash != std::string::npos)
                    variantUrl = url.substr(0, lastSlash + 1) + variantUrl;
            }
            bestUrl = variantUrl;
        }
        pos = lineEnd;
    }

    if (!bestUrl.empty()) {
        std::cout << "HLS: selected variant " << bestUrl << " (" << bestBw / 1000 << " kbps)" << std::endl;
        return bestUrl;
    }
    return url;  // Not a master playlist, use as-is
}

bool VideoDecoder::open(const std::string& source) {
    close();

    // Resolve HLS master playlists to the best single variant
    std::string resolvedSource = resolveHlsVariant(source);
    m_source = resolvedSource;
    m_ff = std::make_unique<FFmpegContext>();

    // Set options for network streams
    AVDictionary* opts = nullptr;
    bool isStream = resolvedSource.find("://") != std::string::npos;
    if (isStream) {
        av_dict_set(&opts, "rtsp_transport", "tcp", 0);
        av_dict_set(&opts, "stimeout", "5000000", 0);   // 5s timeout for RTSP
        av_dict_set(&opts, "timeout", "5000000", 0);     // general timeout
        av_dict_set(&opts, "reconnect", "1", 0);
        av_dict_set(&opts, "reconnect_streamed", "1", 0);
        av_dict_set(&opts, "reconnect_delay_max", "5", 0);
        av_dict_set(&opts, "allowed_extensions", "ALL", 0);
    }

    int ret = avformat_open_input(&m_ff->formatCtx, resolvedSource.c_str(), nullptr, &opts);
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

    // Find best video stream (highest resolution/bitrate)
    m_ff->videoStreamIndex = av_find_best_stream(
        m_ff->formatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (m_ff->videoStreamIndex < 0) {
        std::cerr << "No video stream found in source" << std::endl;
        m_ff.reset();
        return false;
    }

    // For HLS master playlists, FFmpeg opens all variants as separate streams.
    // Discard all video streams except the one we selected to avoid decoding
    // multiple variants simultaneously.
    for (unsigned int i = 0; i < m_ff->formatCtx->nb_streams; i++) {
        if ((int)i != m_ff->videoStreamIndex) {
            m_ff->formatCtx->streams[i]->discard = AVDISCARD_ALL;
        }
    }

    AVStream* stream = m_ff->formatCtx->streams[m_ff->videoStreamIndex];

    // Try hardware decoders: VA-API (Intel/AMD), CUDA/NVDEC (NVIDIA), then software
    const AVCodec* codec = nullptr;
    bool hwDecode = false;

    struct HwBackend {
        AVHWDeviceType type;
        const char* name;
    };
    HwBackend hwBackends[] = {
        { AV_HWDEVICE_TYPE_VAAPI, "VA-API" },
        { AV_HWDEVICE_TYPE_CUDA,  "NVDEC/CUDA" },
    };

    for (auto& hw : hwBackends) {
        if (hwDecode) break;
        AVBufferRef* hwDeviceCtx = nullptr;
        if (av_hwdevice_ctx_create(&hwDeviceCtx, hw.type, nullptr, nullptr, 0) != 0)
            continue;

        codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (codec) {
            m_ff->codecCtx = avcodec_alloc_context3(codec);
            avcodec_parameters_to_context(m_ff->codecCtx, stream->codecpar);
            m_ff->codecCtx->hw_device_ctx = av_buffer_ref(hwDeviceCtx);

            if (avcodec_open2(m_ff->codecCtx, codec, nullptr) == 0) {
                hwDecode = true;
                std::cout << "Video decoder: " << hw.name << " hardware acceleration enabled" << std::endl;
            } else {
                avcodec_free_context(&m_ff->codecCtx);
                m_ff->codecCtx = nullptr;
            }
        }
        av_buffer_unref(&hwDeviceCtx);
    }

    // Fall back to software decoder
    if (!hwDecode) {
        codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!codec) {
            std::cerr << "Unsupported codec: " << avcodec_get_name(stream->codecpar->codec_id) << std::endl;
            m_ff.reset();
            return false;
        }

        m_ff->codecCtx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(m_ff->codecCtx, stream->codecpar);
        m_ff->codecCtx->thread_count = 0;
        m_ff->codecCtx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;

        if (avcodec_open2(m_ff->codecCtx, codec, nullptr) < 0) {
            std::cerr << "Failed to open codec" << std::endl;
            m_ff.reset();
            return false;
        }
        std::cout << "Video decoder: software decoding" << std::endl;
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
    std::lock_guard<std::mutex> lock(m_frameMutex);
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
    size_t rgbaSize = static_cast<size_t>(w) * h * 4;

    // Frame pacing setup
    AVStream* stream = m_ff->formatCtx->streams[m_ff->videoStreamIndex];
    double fps = 30.0;
    if (stream->avg_frame_rate.den > 0)
        fps = av_q2d(stream->avg_frame_rate);
    else if (stream->r_frame_rate.den > 0)
        fps = av_q2d(stream->r_frame_rate);

    auto frameDuration = std::chrono::microseconds((int64_t)(1000000.0 / fps));
    bool isStream = m_source.find("://") != std::string::npos;

    // PTS-based pacing for streams - tracks wall clock vs presentation time
    double timeBase = av_q2d(stream->time_base);
    int64_t firstPts = AV_NOPTS_VALUE;
    auto playbackStart = std::chrono::steady_clock::now();

    while (m_running) {
        auto frameStart = std::chrono::steady_clock::now();

        int ret = av_read_frame(m_ff->formatCtx, m_ff->packet);
        if (ret < 0) {
            if (ret == AVERROR_EOF || ret == AVERROR(EIO)) {
                if (!isStream && m_loop) {
                    av_seek_frame(m_ff->formatCtx, m_ff->videoStreamIndex, 0, AVSEEK_FLAG_BACKWARD);
                    avcodec_flush_buffers(m_ff->codecCtx);
                    continue;
                }
                if (isStream) {
                    // Live stream error - flush decoder and wait before retry
                    avcodec_flush_buffers(m_ff->codecCtx);
                    firstPts = AV_NOPTS_VALUE;
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    continue;
                }
                m_active = false;
                if (m_onEndCallback) {
                    m_onEndCallback();
                }
                break;
            }
            // Transient error - brief pause then retry
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
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
            // PTS-based pacing
            if (m_ff->frame->pts != AV_NOPTS_VALUE) {
                if (firstPts == AV_NOPTS_VALUE) {
                    firstPts = m_ff->frame->pts;
                    playbackStart = std::chrono::steady_clock::now();
                }
                double frameSec = (m_ff->frame->pts - firstPts) * timeBase;
                // Detect PTS discontinuity (jump > 2s or backward) and reset
                auto now = std::chrono::steady_clock::now();
                double wallSec = std::chrono::duration<double>(now - playbackStart).count();
                double drift = frameSec - wallSec;
                if (drift > 2.0 || drift < -2.0) {
                    firstPts = m_ff->frame->pts;
                    playbackStart = now;
                } else if (drift > 0) {
                    std::this_thread::sleep_for(std::chrono::microseconds((int64_t)(drift * 1000000.0)));
                }
            } else if (!isStream) {
                auto elapsed = std::chrono::steady_clock::now() - frameStart;
                auto remaining = frameDuration - elapsed;
                if (remaining.count() > 0) {
                    std::this_thread::sleep_for(remaining);
                }
            }

            // Transfer hw frame to system memory if needed (VA-API/CUDA)
            AVFrame* srcFrame = m_ff->frame;
            AVFrame* swFrame = nullptr;
            if (m_ff->frame->format == AV_PIX_FMT_VAAPI ||
                m_ff->frame->format == AV_PIX_FMT_CUDA) {
                swFrame = av_frame_alloc();
                if (av_hwframe_transfer_data(swFrame, m_ff->frame, 0) < 0) {
                    av_frame_free(&swFrame);
                    continue;
                }
                srcFrame = swFrame;
            }

            // NV12 from hw decoder: pass Y+UV planes directly to GL (no sws_scale)
            if (srcFrame->format == AV_PIX_FMT_NV12) {
                size_t ySize = (size_t)srcFrame->linesize[0] * h;
                size_t uvSize = (size_t)srcFrame->linesize[1] * (h / 2);
                std::vector<unsigned char> pixels(ySize + uvSize);

                // Copy Y plane
                if (srcFrame->linesize[0] == w) {
                    memcpy(pixels.data(), srcFrame->data[0], ySize);
                } else {
                    for (int y2 = 0; y2 < h; y2++)
                        memcpy(pixels.data() + y2 * w, srcFrame->data[0] + y2 * srcFrame->linesize[0], w);
                    ySize = (size_t)w * h;
                }

                // Copy UV plane
                int uvW = w;
                int uvH = h / 2;
                if (srcFrame->linesize[1] == uvW) {
                    memcpy(pixels.data() + ySize, srcFrame->data[1], uvSize);
                } else {
                    for (int y2 = 0; y2 < uvH; y2++)
                        memcpy(pixels.data() + ySize + y2 * uvW, srcFrame->data[1] + y2 * srcFrame->linesize[1], uvW);
                }

                if (swFrame) av_frame_free(&swFrame);

                {
                    std::lock_guard<std::mutex> lock(m_frameMutex);
                    m_currentFrame.setOwnedPixels(std::move(pixels), w, h, 1, ColorFormat::NV12);
                    m_newFrameAvailable = true;
                }
                continue;
            }

            // Software decode path: sws_scale to RGBA
            if (srcFrame->format != m_ff->lastPixFmt) {
                if (m_ff->swsCtx) sws_freeContext(m_ff->swsCtx);
                m_ff->swsCtx = sws_getContext(
                    w, h, (AVPixelFormat)srcFrame->format,
                    w, h, AV_PIX_FMT_RGBA,
                    SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
                m_ff->lastPixFmt = srcFrame->format;
            }

            sws_scale(m_ff->swsCtx,
                      srcFrame->data, srcFrame->linesize,
                      0, h,
                      m_ff->rgbaFrame->data, m_ff->rgbaFrame->linesize);

            if (swFrame) av_frame_free(&swFrame);

            std::vector<unsigned char> pixels(rgbaSize);
            int dstStride = w * 4;
            if (m_ff->rgbaFrame->linesize[0] == dstStride) {
                memcpy(pixels.data(), m_ff->rgbaFrame->data[0], rgbaSize);
            } else {
                for (int y2 = 0; y2 < h; y2++) {
                    memcpy(pixels.data() + y2 * dstStride,
                           m_ff->rgbaFrame->data[0] + y2 * m_ff->rgbaFrame->linesize[0],
                           dstStride);
                }
            }

            {
                std::lock_guard<std::mutex> lock(m_frameMutex);
                m_currentFrame.setOwnedPixels(std::move(pixels), w, h, 4, ColorFormat::RGBA);
                m_newFrameAvailable = true;
            }
        }
    }
}

#endif // HAVE_FFMPEG
