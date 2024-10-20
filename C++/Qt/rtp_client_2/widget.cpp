#include "widget.h"
#include <QDebug>

VideoWidget::VideoWidget(QWidget *parent) : QLabel(parent) {
    this->setAlignment(Qt::AlignCenter);
}

void VideoWidget::displayFrame(const QImage &image) {
    setPixmap(QPixmap::fromImage(image).scaled(size(), Qt::KeepAspectRatio));
}

SDPReceiver::SDPReceiver(VideoWidget *widget, QObject *parent)
    : QObject(parent), videoWidget(widget), formatContext(nullptr),
    codecContext(nullptr), frame(nullptr), packet(nullptr),
    swsContext(nullptr) {}

SDPReceiver::~SDPReceiver() {
    if (swsContext) sws_freeContext(swsContext);
    if (frame) av_frame_free(&frame);
    if (packet) av_packet_free(&packet);
    if (codecContext) avcodec_free_context(&codecContext);
    if (formatContext) avformat_close_input(&formatContext);
}

void SDPReceiver::startReceiving(const QString &sdpFilePath) {
    avformat_network_init();

    // 프로토콜 화이트리스트 추가
    AVDictionary *options = nullptr;
    av_dict_set(&options, "protocol_whitelist", "file,udp,rtp", 0);
    av_dict_set(&options, "buffer_size",  "1048576", 0);  // 버퍼 크기를 1MB로 설정
    av_dict_set(&options, "probesize", "5000000", 0);  // 프로브 크기 증가
    av_dict_set(&options, "analyzeduration", "10000000", 0);  // 분석 시간 증가
    av_dict_set(&options, "max_delay", "500000", 0);  // 지터 허용 범위 증가

    // SDP 파일에서 스트림 열기
    if (avformat_open_input(&formatContext, sdpFilePath.toStdString().c_str(), nullptr,  &options) != 0) {
        qDebug() << "Failed to open SDP file.";
        return;
    }
    av_dict_free(&options);

    // 스트림 정보 읽기
    if (avformat_find_stream_info(formatContext, nullptr) < 0) {
        qDebug() << "Failed to retrieve stream info.";
        return;
    }

    // 비디오 스트림 찾기
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

    // 비디오 코덱 찾기 및 초기화
    const AVCodec *codec = avcodec_find_decoder(formatContext->streams[videoStreamIndex]->codecpar->codec_id);
    if (!codec) {
        qDebug() << "Failed to find codec.";
        return;
    }

    codecContext = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codecContext, formatContext->streams[videoStreamIndex]->codecpar);

    // 추가 설정: 프레임 스킵 및 스레드 설정
    codecContext->thread_count = 4;  // 스레드 수 설정
    codecContext->skip_frame = AVDISCARD_NONREF;  // 참조되지 않는 프레임을 건너뜀

    if (avcodec_open2(codecContext, codec, nullptr) < 0) {
        qDebug() << "Failed to open codec.";
        return;
    }

    // 패킷 및 프레임 메모리 할당
    packet = av_packet_alloc();
    frame = av_frame_alloc();

    // YUV -> RGB 변환을 위한 컨텍스트를 한 번만 설정
    int width = codecContext->width;
    int height = codecContext->height;
    swsContext = sws_getContext(width, height, codecContext->pix_fmt,
                                width, height, AV_PIX_FMT_RGB24,
                                SWS_FAST_BILINEAR | SWS_FULL_CHR_H_INP, nullptr, nullptr, nullptr);

    // 화면 갱신을 위한 타이머 설정
    QTimer *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &SDPReceiver::decodeFrame);
    timer->start(33);  // 약 30fps로 갱신
}
void SDPReceiver::decodeFrame() {
    int ret = av_read_frame(formatContext, packet);
    if (ret < 0) {
        if (ret == AVERROR(EAGAIN)) {
            // 패킷이 아직 준비되지 않음
            return;
        } else {
            qDebug() << "Failed to read frame: " << ret;
            return;
        }
    }

    if (packet->stream_index == formatContext->streams[0]->index) {
        if (avcodec_send_packet(codecContext, packet) == 0) {
            if (avcodec_receive_frame(codecContext, frame) == 0) {
                // 프레임의 원본 해상도와 픽셀 포맷 확인
                int width = frame->width;
                int height = frame->height;
                // QImage에 맞는 RGB 포맷으로 변환
                QImage image(width, height, QImage::Format_RGB888);

                uint8_t *dest[4] = {image.bits(), nullptr, nullptr, nullptr};
                int destLinesize[4] = {static_cast<int>(image.bytesPerLine()), 0, 0, 0};

                sws_scale(swsContext, frame->data, frame->linesize, 0, height, dest, destLinesize);

                // QLabel에 프레임을 표시
                videoWidget->displayFrame(image);
            }
        }
    }
    av_packet_unref(packet);
}

