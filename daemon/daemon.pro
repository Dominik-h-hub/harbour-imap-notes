TEMPLATE = app
TARGET = harbour-imap-notes-daemon
CONFIG -= app_bundle
CONFIG += c++14

QT += core network sql
QT -= gui

INCLUDEPATH += $$PWD/../src

SOURCES += \
    main.cpp \
    $$PWD/../src/syncworker.cpp \
    $$PWD/../src/imapclient.cpp \
    $$PWD/../src/notemessage.cpp \
    $$PWD/../src/note.cpp

HEADERS += \
    $$PWD/../src/syncworker.h \
    $$PWD/../src/imapclient.h \
    $$PWD/../src/notemessage.h \
    $$PWD/../src/note.h

target.path = /usr/bin
INSTALLS += target

systemd_unit.files = $$PWD/../systemd/harbour-imap-notes-daemon.service
systemd_unit.path = /usr/lib/systemd/user
INSTALLS += systemd_unit
