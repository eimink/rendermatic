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
#ifdef __linux__
#include <libavutil/hwcontext_vaapi.h>
#endif
}

#ifdef __linux__
#include <va/va.h>
#include <va/va_drmcommon.h>
#endif
#include <unistd.h>

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
    bool hwDecode = false;
    bool dmaBufFailed = false;
    double firstPts = -1.0;
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
static std::string resolveHlsVariant(const std::string& url) {
    if (url.find(".m3u8") == std::string::npos)
        return url;

    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "timeout", "5000000", 0);
    AVIOContext* io = nullptr;
    if (avio_open2(&io, url.c_str(), AVIO_FLAG_READ, nullptr, &opts) < 0) {
        if (opts) av_dict_free(&opts);
        return url;
    }
    if (opts) av_dict_free(&opts);

    char buf[65536];
    int len = avio_read(io, (unsigned char*)buf, sizeof(buf) - 1);
    avio_closep(&io);
    if (len <= 0) return url;
    buf[len] = '\0';

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
    return url;
}

bool VideoDecoder::open(const std::string& source) {
    close();

    std::string resolvedSource = resolveHlsVariant(source);
    m_source = resolvedSource;
    m_isStream = resolvedSource.find("://") != std::string::npos;
    m_ff = std::make_unique<FFmpegContext>();

    AVDictionary* opts = nullptr;
    if (m_isStream) {
        av_dict_set(&opts, "rtsp_transport", "tcp", 0);
        av_dict_set(&opts, "stimeout", "5000000", 0);
        av_dict_set(&opts, "timeout", "5000000", 0);
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

    m_ff->videoStreamIndex = av_find_best_stream(
        m_ff->formatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (m_ff->videoStreamIndex < 0) {
        std::cerr << "No video stream found in source" << std::endl;
        m_ff.reset();
        return false;
    }

    for (unsigned int i = 0; i < m_ff->formatCtx->nb_streams; i++) {
        if ((int)i != m_ff->videoStreamIndex)
            m_ff->formatCtx->streams[i]->discard = AVDISCARD_ALL;
    }

    AVStream* stream = m_ff->formatCtx->streams[m_ff->videoStreamIndex];
    m_timeBase = av_q2d(stream->time_base);

    const AVCodec* codec = nullptr;
    bool hwDecode = false;

#ifdef __linux__
    // Try VA-API hardware decode
    AVBufferRef* hwDeviceCtx = nullptr;
    if (av_hwdevice_ctx_create(&hwDeviceCtx, AV_HWDEVICE_TYPE_VAAPI, nullptr, nullptr, 0) == 0) {
        codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (codec) {
            m_ff->codecCtx = avcodec_alloc_context3(codec);
            avcodec_parameters_to_context(m_ff->codecCtx, stream->codecpar);
            m_ff->codecCtx->hw_device_ctx = av_buffer_ref(hwDeviceCtx);
            if (avcodec_open2(m_ff->codecCtx, codec, nullptr) == 0) {
                hwDecode = true;
                m_ff->hwDecode = true;
                std::cout << "Video decoder: VA-API hardware" << std::endl;
            } else {
                avcodec_free_context(&m_ff->codecCtx);
                m_ff->codecCtx = nullptr;
            }
        }
        av_buffer_unref(&hwDeviceCtx);
    }
#endif

    if (!hwDecode) {
        codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!codec) {
            std::cerr << "Unsupported codec: " << avcodec_get_name(stream->codecpar->codec_id) << std::endl;
            m_ff.reset();
            return false;
        }
        m_ff->codecCtx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(m_ff->codecCtx, stream->codecpar);
        m_ff->codecCtx->thread_count = 0;  // auto - use all cores
        m_ff->codecCtx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
        if (avcodec_open2(m_ff->codecCtx, codec, nullptr) < 0) {
            std::cerr << "Failed to open codec" << std::endl;
            m_ff.reset();
            return false;
        }
        std::cout << "Video decoder: software (" << m_ff->codecCtx->thread_count << " threads)" << std::endl;
    }

    m_ff->frame = av_frame_alloc();
    m_ff->rgbaFrame = av_frame_alloc();
    m_ff->packet = av_packet_alloc();

    int w = m_ff->codecCtx->width;
    int h = m_ff->codecCtx->height;
    int bufSize = av_image_get_buffer_size(AV_PIX_FMT_RGBA, w, h, 1);
    m_ff->rgbaBuffer = (uint8_t*)av_malloc(bufSize);
    av_image_fill_arrays(m_ff->rgbaFrame->data, m_ff->rgbaFrame->linesize,
                         m_ff->rgbaBuffer, AV_PIX_FMT_RGBA, w, h, 1);

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
    m_frameQueue.stop();
    if (m_thread.joinable())
        m_thread.join();
}

bool VideoDecoder::getFrameForTime(double mediaTime, Texture& outTexture) {
    return m_frameQueue.getFrameForTime(mediaTime, outTexture);
}

bool VideoDecoder::getLatestFrame(Texture& outTexture) {
    return m_frameQueue.getLatest(outTexture);
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
    if (stream->avg_frame_rate.den > 0)
        info.fps = av_q2d(stream->avg_frame_rate);
    else if (stream->r_frame_rate.den > 0)
        info.fps = av_q2d(stream->r_frame_rate);

    if (m_ff->formatCtx->duration > 0)
        info.duration = (double)m_ff->formatCtx->duration / AV_TIME_BASE;

    return info;
}

void VideoDecoder::decoderLoop() {
    if (!m_ff) return;

    int w = m_ff->codecCtx->width;
    int h = m_ff->codecCtx->height;
    size_t rgbaSize = static_cast<size_t>(w) * h * 4;

    int decodedFrames = 0;
    auto lastDecoderLog = std::chrono::steady_clock::now();

    while (m_running) {
        int ret = av_read_frame(m_ff->formatCtx, m_ff->packet);
        if (ret < 0) {
            if (ret == AVERROR_EOF || ret == AVERROR(EIO)) {
                if (!m_isStream && m_loop) {
                    av_seek_frame(m_ff->formatCtx, m_ff->videoStreamIndex, 0, AVSEEK_FLAG_BACKWARD);
                    avcodec_flush_buffers(m_ff->codecCtx);
                    continue;
                }
                if (m_isStream) {
                    avcodec_flush_buffers(m_ff->codecCtx);
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    continue;
                }
                m_active = false;
                if (m_onEndCallback)
                    m_onEndCallback();
                break;
            }
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
            // Calculate PTS in seconds, normalized to start from 0
            double pts = 0.0;
            if (m_ff->frame->pts != AV_NOPTS_VALUE) {
                double absPts = m_ff->frame->pts * m_timeBase;
                if (m_ff->firstPts < 0.0) m_ff->firstPts = absPts;
                pts = absPts - m_ff->firstPts;
            }

#ifdef __linux__
            // VA-API hw decode path
            if (m_ff->hwDecode && m_ff->frame->format == AV_PIX_FMT_VAAPI) {
                bool exported = false;

                if (!m_ff->dmaBufFailed) {
                    VADRMPRIMESurfaceDescriptor desc = {};
                    VASurfaceID surface = (VASurfaceID)(uintptr_t)m_ff->frame->data[3];
                    AVHWDeviceContext* devCtx = (AVHWDeviceContext*)m_ff->codecCtx->hw_device_ctx->data;
                    AVVAAPIDeviceContext* vaCtx = (AVVAAPIDeviceContext*)devCtx->hwctx;

                    vaSyncSurface(vaCtx->display, surface);

                    VAStatus st = vaExportSurfaceHandle(
                        vaCtx->display, surface,
                        VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
                        VA_EXPORT_SURFACE_READ_ONLY | VA_EXPORT_SURFACE_COMPOSED_LAYERS,
                        &desc);

                    if (st == VA_STATUS_SUCCESS && desc.num_layers >= 1) {
                        Texture dmaTex;
                        dmaTex.width = w;
                        dmaTex.height = h;
                        dmaTex.format = ColorFormat::DMABUF_NV12;
                        dmaTex.dmaFd = desc.objects[0].fd;
                        dmaTex.dmaFourcc = desc.fourcc;
                        dmaTex.dmaOffset[0] = desc.layers[0].offset[0];
                        dmaTex.dmaPitch[0] = desc.layers[0].pitch[0];
                        if (desc.num_layers > 1) {
                            dmaTex.dmaOffset[1] = desc.layers[1].offset[0];
                            dmaTex.dmaPitch[1] = desc.layers[1].pitch[0];
                        } else if (desc.layers[0].num_planes > 1) {
                            dmaTex.dmaOffset[1] = desc.layers[0].offset[1];
                            dmaTex.dmaPitch[1] = desc.layers[0].pitch[1];
                        }
                        m_frameQueue.push(pts, std::move(dmaTex));
                        exported = true;
                    } else {
                        std::cerr << "DMA-BUF export not supported (status " << st
                                  << "), falling back to transfer" << std::endl;
                        m_ff->dmaBufFailed = true;
                    }
                }

                if (!exported) {
                    // Transfer to CPU, pass as NV12
                    AVFrame* swFrame = av_frame_alloc();
                    if (av_hwframe_transfer_data(swFrame, m_ff->frame, 0) == 0) {
                        size_t ySize = (size_t)w * h;
                        size_t uvSize = (size_t)w * (h / 2);
                        std::vector<unsigned char> pixels(ySize + uvSize);
                        for (int y = 0; y < h; y++)
                            memcpy(pixels.data() + y * w, swFrame->data[0] + y * swFrame->linesize[0], w);
                        for (int y = 0; y < h / 2; y++)
                            memcpy(pixels.data() + ySize + y * w, swFrame->data[1] + y * swFrame->linesize[1], w);

                        Texture nv12Tex;
                        nv12Tex.setOwnedPixels(std::move(pixels), w, h, 1, ColorFormat::NV12);
                        m_frameQueue.push(pts, std::move(nv12Tex));
                    }
                    av_frame_free(&swFrame);
                }
                continue;
            }
#endif

            // Software decode: sws_scale to RGBA
            AVFrame* srcFrame = m_ff->frame;

            if (srcFrame->format != m_ff->lastPixFmt) {
                if (m_ff->swsCtx) sws_freeContext(m_ff->swsCtx);
                m_ff->swsCtx = sws_getContext(
                    w, h, (AVPixelFormat)srcFrame->format,
                    w, h, AV_PIX_FMT_RGBA,
                    SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
                m_ff->lastPixFmt = srcFrame->format;
            }

            if (!m_ff->swsCtx) continue;

            sws_scale(m_ff->swsCtx,
                      srcFrame->data, srcFrame->linesize,
                      0, h,
                      m_ff->rgbaFrame->data, m_ff->rgbaFrame->linesize);

            std::vector<unsigned char> pixels(rgbaSize);
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

            Texture rgbaTex;
            rgbaTex.setOwnedPixels(std::move(pixels), w, h, 4, ColorFormat::RGBA);
            m_frameQueue.push(pts, std::move(rgbaTex));

            decodedFrames++;
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration<double>(now - lastDecoderLog).count() >= 5.0) {
                double elapsed = std::chrono::duration<double>(now - lastDecoderLog).count();
                std::cout << "Decoder: " << (decodedFrames / elapsed) << " fps"
                          << " | pts: " << pts << "s"
                          << " | queue: " << m_frameQueue.size()
                          << std::endl;
                decodedFrames = 0;
                lastDecoderLog = now;
            }

            // For files: don't decode more than 1s ahead of playback.
            // Sleep until playback catches up.
            if (!m_isStream) {
                double playback = m_playbackTime.load();
                while (m_running && pts > playback + 1.0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    playback = m_playbackTime.load();
                }
            }
        }
    }
}

#endif // HAVE_FFMPEG
