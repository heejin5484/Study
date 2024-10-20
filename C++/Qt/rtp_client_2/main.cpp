#include <QApplication>
#include "widget.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    // QLabel을 통한 비디오 표시
    VideoWidget videoWidget;
    videoWidget.resize(800, 600);
    videoWidget.show();

    // SDPReceiver를 통해 SDP 파일 기반 RTP 스트림 수신
    SDPReceiver receiver(&videoWidget);
    receiver.startReceiving("C:/Users/3kati/Desktop/Qt/rtsp/rtsp/build/Desktop_Qt_6_7_2_MSVC2019_64bit-Debug/stream.sdp");  // SDP 파일 경로 설정

    return app.exec();
}

//#include "moc_widget.cpp"
