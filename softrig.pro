

QT       += core gui multimedia

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = softrig
TEMPLATE = app

# emit warnings when using deprecated features
DEFINES += QT_DEPRECATED_WARNINGS

# disables all the APIs deprecated before Qt 6.0.0
DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000

SOURCES += \
    app/main.cpp \
    gui/device_config.cpp \
    gui/mainwindow.cpp

HEADERS += \
    gui/device_config.h \
    gui/mainwindow.h \
    util/qthread_wrapper.h

FORMS += \
    gui/device_config.ui \
    gui/mainwindow.ui

