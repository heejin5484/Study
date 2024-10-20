#include "rtpreceiver.h"
#include <QTimer>
#include <QDebug>

RTPReceiver::RTPReceiver(VideoWidget *widget, QObject *parent)
    : QObject(parent), videoWidget(widget), formatContext(nullptr),
    codecContext(nullptr), frame(nullptr), packet(nullptr),
    swsContext(nullptr) {}

RTPReceiver::~RTPReceiver() {
    if (swsContext) sws_freeContext(swsContext);
    if (frame) av_frame_free(&frame);
    if (packet) av_packet_free(&packet);
    if (codecContext) avcodec_free_context(&codecContext);
    if (formatContext) avformat_close_input(&formatContext);
}

void RTPReceiver::startReceiving(const QString &rtpUrl) {
    avformat_network_init();

    if (avformat_open_input(&formatContext, rtpUrl.toStdString().c_str(), nullptr, nullptr) != 0) {
        qDebug() << "Failed to open RTP stream.";
        return;
    }

    if (avformat_find_stream_info(formatContext, nullptr) < 0) {
        qDebug() << "Failed to retrieve stream info.";
        return;
    }

    int videoStreamIndex = -1;
    for (unsigned int i = 0; i < formatContext->nb_streams; i++) {
        if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIndex = i;
            break;
        }
    }

    if (videoStreamIndex == -1) {
        qDebug() << "Failed to find video stream.";
        return;
    }

    const AVCodec *codec = avcodec_find_decoder(formatContext->streams[videoStreamIndex]->codecpar->codec_id);
    if (!codec) {
        qDebug() << "Failed to find codec.";
        return;
    }

    codecContext = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codecContext, formatContext->streams[videoStreamIndex]->codecpar);
    if (avcodec_open2(codecContext, codec, nullptr) < 0) {
        qDebug() << "Failed to open codec.";
        return;
    }

    packet = av_packet_alloc();
    frame = av_frame_alloc();

    QTimer *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &RTPReceiver::decodeFrame);
    timer->start(30);
}

void RTPReceiver::decodeFrame() {
    if (av_read_frame(formatContext, packet) >= 0) {
        if (packet->stream_index == formatContext->streams[0]->index) {
            if (avcodec_send_packet(codecContext, packet) == 0) {
                if (avcodec_receive_frame(codecContext, frame) == 0) {
                    int width = frame->width;
                    int height = frame->height;
                    QImage image(width, height, QImage::Format_RGB888);

                    swsContext = sws_getContext(width, height, codecContext->pix_fmt,
                                                width, height, AV_PIX_FMT_RGB24,
                                                SWS_BILINEAR, nullptr, nullptr, nullptr);
                    uint8_t *dest[4] = {image.bits(), nullptr, nullptr, nullptr};
                    int destLinesize[4] = {static_cast<int>(image.bytesPerLine()), 0, 0, 0};

                    sws_scale(swsContext, frame->data, frame->linesize, 0, height, dest, destLinesize);

                    videoWidget->displayFrame(image);
                }
            }
        }
        av_packet_unref(packet);
    }
}
