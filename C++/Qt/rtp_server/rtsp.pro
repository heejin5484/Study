QT       += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17
# GStreamer 관련 헤더 경로 추가
INCLUDEPATH += C:/gstreamer/1.0/msvc_x86_64/include/gstreamer-1.0
# GLib 라이브러리 경로 추가
INCLUDEPATH += C:/gstreamer/1.0/msvc_x86_64/include/glib-2.0

INCLUDEPATH += C:/gstreamer/1.0/msvc_x86_64/lib/glib-2.0/include

# GStreamer 라이브러리 경로 추가
LIBS += -LC:/gstreamer/1.0/msvc_x86_64/lib -lgstreamer-1.0 -lgstrtspserver-1.0

# GStreamer와 GLib 라이브러리 링크 (중복 제거)
LIBS += -lgobject-2.0 -lglib-2.0 -lgmodule-2.0 -lgthread-2.0

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \
    widget.cpp

HEADERS += \
    widget.h

FORMS += \
    widget.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
