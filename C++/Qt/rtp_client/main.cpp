#include <QApplication>
#include <QProcess>
#include <QDebug>
#include <QImage>
#include <QLabel>

QProcess *ffmpegProcess;
QLabel *videoLabel;

void readFFmpegOutput() {
    QByteArray data = ffmpegProcess->readAllStandardOutput();

    // 예시: 데이터의 크기 확인 (디버그용)
    qDebug() << "Received data size:" << data.size();

    // raw RGB 데이터로 가정하고 QImage로 변환
    if (data.size() >= 640 * 480 * 3) {  // 해상도 640x480에 대해 RGB는 3바이트 픽셀
        QImage image(reinterpret_cast<const uchar*>(data.data()), 640, 480, QImage::Format_RGB888);
        videoLabel->setPixmap(QPixmap::fromImage(image).scaled(videoLabel->size(), Qt::KeepAspectRatio));
    }
}

void startFFmpegProcess() {
    ffmpegProcess = new QProcess();

    // FFmpeg 실행 경로 및 명령어 설정
    QString program = "C:/ffmpeg/ffmpeg-n5.1-latest-win64-gpl-shared-5.1/bin/ffmpeg.exe";
    QStringList arguments;

    // FFmpeg 명령어 설정 (rawvideo를 stdout으로 출력하도록)
    arguments << "-protocol_whitelist" << "file,udp,rtp"
              << "-i" << "C:/Users/3kati/Desktop/Qt/rtsp/rtsp/build/Desktop_Qt_6_7_2_MSVC2019_64bit-Debug/stream.sdp"
              << "-s" << "640x480"
              << "-pix_fmt" << "rgb24"  // 픽셀 포맷을 raw RGB로 설정
              << "-f" << "rawvideo"  // 출력을 raw 비디오로 설정
              << "-";  // stdout으로 출력

    // QProcess로 FFmpeg 실행
    ffmpegProcess->start(program, arguments);

    if (!ffmpegProcess->waitForStarted()) {
        qDebug() << "FFmpeg 실행 실패: " << ffmpegProcess->errorString();
    } else {
        qDebug() << "FFmpeg 스트리밍 시작 중...";
        QObject::connect(ffmpegProcess, &QProcess::readyReadStandardOutput, &readFFmpegOutput);  // 수정된 부분
    }
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    // QLabel을 사용하여 QImage를 화면에 표시
    QWidget window;
    videoLabel = new QLabel(&window);
    videoLabel->setFixedSize(640, 480);
    window.show();

    // FFmpeg 프로세스 시작
    startFFmpegProcess();

    return app.exec();
}
