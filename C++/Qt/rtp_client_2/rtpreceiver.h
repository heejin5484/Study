#ifndef RTPRECEIVER_H
#define RTPRECEIVER_H

#include <QObject>
extern "C"{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
}
#include "widget.h"

class RTPReceiver : public QObject {
    Q_OBJECT

public:
    RTPReceiver(VideoWidget *widget, QObject *parent = nullptr);
    ~RTPReceiver();

    void startReceiving(const QString &rtpUrl);

private slots:
    void decodeFrame();

private:
    VideoWidget *videoWidget;
    AVFormatContext *formatContext;
    AVCodecContext *codecContext;
    AVFrame *frame;
    AVPacket *packet;
    SwsContext *swsContext;
};

#endif // RTPRECEIVER_H
