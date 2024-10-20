#ifndef WIDGET_H
#define WIDGET_H

#include <QLabel>
#include <QObject>
#include <QImage>
#include <QTimer>
#include <QPixmap>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}

// QLabel을 통해 비디오를 표시하는 클래스
class VideoWidget : public QLabel {
    Q_OBJECT

public:
    VideoWidget(QWidget *parent = nullptr);

    // QImage로 변환된 프레임을 QLabel에 업데이트
    void displayFrame(const QImage &image);
};

// FFmpeg을 사용하여 SDP 파일로부터 RTP 스트림을 받아오는 클래스
class SDPReceiver : public QObject {
    Q_OBJECT

public:
    SDPReceiver(VideoWidget *widget, QObject *parent = nullptr);
    ~SDPReceiver();

    // SDP 파일을 이용한 RTP 스트림 수신 시작
    void startReceiving(const QString &sdpFilePath);

private slots:
    // 프레임 디코딩 및 QLabel에 업데이트
    void decodeFrame();

private:
    VideoWidget *videoWidget;
    AVFormatContext *formatContext;
    AVCodecContext *codecContext;
    AVFrame *frame;
    AVPacket *packet;
    SwsContext *swsContext;
};

#endif // WIDGET_H
