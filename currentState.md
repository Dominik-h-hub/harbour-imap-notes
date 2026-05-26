Build-System
- harbour-imap-notes.pro — App, Qt5 (qml/quick/sql/network/dbus) + libetpan via pkg-config
- daemon/daemon.pro — separater Background-Daemon, baut nach harbour-imap-notes-daemon
- rpm/harbour-imap-notes.spec — baut beide Binaries in einem mb2 build, installiert systemd-User-Unit nach /usr/lib/systemd/user/

C++ Backend (src/)
- NotesDatabase — SQLite mit Schema v1, WAL, FTS5-Volltextsuche via Trigger, PRAGMA user_version-Migrations
- NoteMessage — RFC2822-Codec mit Apple-Notes-Headern (X-Uniform-Type-Identifier, X-Universally-Unique-Identifier, X-Mail-Created-Date, X-Last-Modified, X-ImapNotes-Format), multipart/mixed-Anhänge
- ImapClient — libetpan-Wrapper: SSL/STARTTLS, LIST/CREATE/RENAME/DELETE, UID FETCH/APPEND/STORE/EXPUNGE/MOVE, IDLE mit Self-Pipe-Cancel, UIDPLUS+MOVE-Detection
- SyncWorker (auf eigenem QThread, eigene DB-Connection) — Ordner-Reconcile inkl. Auto-Create der Notes-Root, Trash-Discovery, Tombstone-Push mit Edit-wins-over-Delete, Dirty-Note-APPEND mit alter-UID-Expunge, Remote-Pull mit
  UUID-Dedupe, Server-Delete-Detection, Trash-Auto-Cleanup
- SyncEngine — Fassade über Worker mit QTimer (5-Min-Default, einstellbar)
- AccountsManager, NotesManager — QML-CRUD-APIs
- AccountsModel/FoldersModel/NotesModel — QAbstractListModel mit Sortierung Pinned+last-modified DESC
- Settings, NetworkMonitor, RichTextConverter

QML UI (qml/)
- AccountsPage (Top-Nav) → FoldersPage → NotesListPage → NoteEditorPage
- AccountEditPage mit Verbindungstest
- FolderEditDialog (Create/Rename)
- SettingsPage (Sync-Intervall, WLAN-only, Trash-Cleanup)
- TrashPage mit Restore + Delete-Forever
- SearchPage (FTS-Suche)
- CoverPage mit Sync-Status + Quick-Action
- DE-Übersetzungen

Daemon (daemon/, systemd/)
- Eigene Binary, läuft als systemd --user-Service, startet alle 5 Min einen Sync

So testest du

# Im Sailfish SDK Build Engine:
cd /share/SFOSdev/imapNotes   # oder dein Pfad
sfdk config --session target=SailfishOS-4.5.0.18-aarch64
sfdk build
sfdk deploy --sdk

# Auf dem Gerät:
imap-notes &       # App startet
# Account anlegen → Verbindung testen → Sync starten

# Daemon optional:
systemctl --user enable --now harbour-imap-notes-daemon
journalctl --user -u harbour-imap-notes-daemon -f

Bekannte Lücken (für nächste Iterationen)

- Anhänge: UI-Picker und persistenter Speicher in ~/.local/share/.../attachments/ noch offen (Button zeigt nur Status-Hinweis)
- Passwort-Speicherung: aktuell base64-obfuskiert in der DB; sailfish-secrets-Integration fehlt
- CONDSTORE/QRESYNC: Capabilities werden erkannt, aber für incremental sync noch nicht genutzt
- IMAP UTF-7 Mailbox-Encoding: nicht implementiert, funktioniert bei modernen UTF8=ACCEPT-Servern
- Cover-Action „Neue Notiz": entfernt (braucht DBus-Aktivierung der App, das wäre eigene Aufgabe)
- Außenordner imapNotes/ umbenennen — manuell nach Claude-Code-Session: Rename-Item 'C:\Users\PC\SFOSdev\imapNotes' 'harbour-imap-notes'
