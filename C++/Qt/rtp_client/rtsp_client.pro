QT += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# FFmpeg 및 GStreamer 헤더 경로 추가
INCLUDEPATH += C:/ffmpeg/ffmpeg-n5.1-latest-win64-gpl-shared-5.1/include
INCLUDEPATH += C:/gstreamer/1.0/msvc_x86_64/include/gstreamer-1.0
INCLUDEPATH += C:/gstreamer/1.0/msvc_x86_64/include/glib-2.0
INCLUDEPATH += C:/gstreamer/1.0/msvc_x86_64/lib/glib-2.0/include

# FFmpeg 및 GStreamer 라이브러리 경로 추가
LIBS += -LC:/ffmpeg/ffmpeg-n5.1-latest-win64-gpl-shared-5.1/lib -lavcodec -lavformat -lavutil -lswscale
LIBS += -LC:/gstreamer/1.0/msvc_x86_64/lib -lgstreamer-1.0 -lgstrtspserver-1.0
LIBS += -lgobject-2.0 -lglib-2.0 -lgmodule-2.0 -lgthread-2.0

SOURCES += main.cpp \
    widget.cpp \

HEADERS += widget.h \
