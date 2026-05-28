# NOTICE:
#
# Application name defined in TARGET has a corresponding QML filename.
# If name defined in TARGET is changed, the following needs to be done
# to match new name:
#   - corresponding QML filename must be changed
#   - desktop icon filename must be changed
#   - desktop filename must be changed
#   - icon definition filename in desktop file must be changed
#   - translation filenames have to be changed

TARGET = harbour-imap-notes

CONFIG += sailfishapp c++14

QT += core gui qml quick sql network dbus

# IMAP is implemented directly over QSslSocket (Qt5Network) — no external
# library dependency required.

SOURCES += \
    src/harbour-imap-notes.cpp \
    src/notesdatabase.cpp \
    src/settings.cpp \
    src/account.cpp \
    src/accountsmanager.cpp \
    src/accountsmodel.cpp \
    src/foldersmodel.cpp \
    src/notesmodel.cpp \
    src/notesmanager.cpp \
    src/note.cpp \
    src/notemessage.cpp \
    src/imapclient.cpp \
    src/syncengine.cpp \
    src/syncworker.cpp \
    src/networkmonitor.cpp \
    src/richtextconverter.cpp

HEADERS += \
    src/notesdatabase.h \
    src/settings.h \
    src/account.h \
    src/accountsmanager.h \
    src/accountsmodel.h \
    src/foldersmodel.h \
    src/notesmodel.h \
    src/notesmanager.h \
    src/note.h \
    src/notemessage.h \
    src/imapclient.h \
    src/syncengine.h \
    src/syncworker.h \
    src/networkmonitor.h \
    src/richtextconverter.h

DISTFILES += \
    qml/harbour-imap-notes.qml \
    qml/cover/CoverPage.qml \
    qml/pages/AccountsPage.qml \
    qml/pages/AccountEditPage.qml \
    qml/pages/FolderEditDialog.qml \
    qml/pages/FoldersPage.qml \
    qml/pages/NotesListPage.qml \
    qml/pages/NoteEditorPage.qml \
    qml/pages/SearchPage.qml \
    qml/pages/SettingsPage.qml \
    qml/pages/TrashPage.qml \
    qml/components/NoteListItem.qml \
    qml/components/RichTextToolbar.qml \
    qml/components/SyncStatusIndicator.qml \
    rpm/harbour-imap-notes.changes.in \
    rpm/harbour-imap-notes.changes.run.in \
    rpm/harbour-imap-notes.spec \
    systemd/harbour-imap-notes-daemon.service \
    daemon/daemon.pro \
    daemon/main.cpp \
    translations/*.ts \
    harbour-imap-notes.desktop

SAILFISHAPP_ICONS = 86x86 108x108 128x128 172x172

CONFIG += sailfishapp_i18n

TRANSLATIONS += translations/harbour-imap-notes-de.ts

