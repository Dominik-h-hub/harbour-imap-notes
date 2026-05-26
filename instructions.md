Allererster Schritt: Benenne alle nötigen Dateien und inhalte von "imapNotes" in "harbour-imap-notes" um, damit es in Sailfish OS als native App erkannt wird. Alle Dateien, Ordner, Klassen, Funktionen, Variablen etc. die mit "imapNotes" zu tun haben müssen in "harbour-imap-notes" umbenannt werden. Habe ich bei der Projekterstellung falsch benannt.

Ziel: Erstelle einen nativen Sailfish OS Client zur Erstellung, Bearbeitung, Löschung von Notizen, 
der die Synchronisation mit IMAP notes (ähnlich wie die native Notizenapp unter iOS) unterstützt, 
damit ich unter iOS (in der nativen App) und Sailfish OS die gleichen Notizen habe.


Passe die App folgendermaßen an:
- Die Hauptseite soll die synchronisierten Notizen in einer Ordnerstruktur anzeigen, ähnlich wie die native Notizenapp unter iOS.
- Es soll eine Funktion zum Erstellen neuer Notizen geben, die es ermöglicht, Notizen in verschiedenen Ordnern zu organisieren.
- Es soll eine Funktion zum Bearbeiten von Notizen geben, die es ermöglicht, den Inhalt der Notizen zu ändern und sie in andere Ordner zu verschieben.
- Es soll eine Funktion zum Löschen von Notizen geben, die es ermöglicht, Notizen dauerhaft zu entfernen. Wird eine Notiz gelöscht, soll diese in einen Trash-Ordner verschoben werden (iOS-konsistent, undoable), Ordnername: Notes/Deleted Messages als Default, Hierarchie-Trennzeichen vom Server via LIST abfragen (/ oder .)., Auto-Cleanup: nach 30 tagen, einstellbar, Sichtbarkeit in der App: Trash-Ordner in separater Bereich („Zuletzt gelöscht") anzeigen. Restore-Action aus Trash zurück in Original-Ordner möglich.
- Es soll eine Funktion zum Synchronisieren der Notizen geben (manueller synchronisationsbutton)
- Die App soll eine benutzerfreundliche Oberfläche haben, die es einfach macht, Notizen zu erstellen, zu bearbeiten und zu löschen.
- Es soll eine Möglichkeit geben, die Synchronisationseinstellungen anzupassen, z.B. die IMAP-Serveradresse, den Benutzernamen und das Passwort für die Synchronisation.
- Es soll eine Möglichkeit geben, die Synchronisation manuell zu starten, damit die Notizen sofort aktualisiert werden können, ohne auf die automatische Synchronisation warten zu müssen.

App stores:
- openrepos.net

Datenbank-Schema-Versionierung: (PRAGMA user_version) von Anfang an einplanen --> habe da keine Meinung, mache was am besten ist.

Einstellungen (siehe ggf. mehr details unten):
- IMAP Account details
- Synchronisierungsintervall
- Anhänge-Limit: Synchronisierung von Bilder/Anhängen WLAN oder WLAN+Mobilfunk - keine Dateianhangsgrößenbegrenzung
- Default-Ordner für neue Notizen für QuickAction "Neue Notiz"
- Auto-Cleanup für Trash-Ordner (z.B. 30 Tage, einstellbar)

RichText Details:
- Anzeige: TextArea mit textFormat: RichText
- Editieren: Toolbar-Buttons (Bold/Italic/Listen) bauen + den HTML-String manipulieren
- Zusätzlich: Überschrift (eine Überschriftart reicht), Checkbox-Bullets inkl. an- und abhaken möglich.
  --> Einfache Formatierung (Überschrift/B/I/U/Listen/Checkbox-Bullets) im Editor, Bilder nur als Anhang am Ende

Bilder und Anhänge:
- Bilder oder Anhänge hinzufügen aus der App möglich - Auswahl aus dem Filebrowser (könnten dann Anhänge sein, oder Bilder)
- Anhänge speichern unter  ~/.local/share/.../attachments/
- WLAN-only-Modus + neue Notiz mit großem Anhang im Mobilfunk → Header runterladen, Anhang lazy beim nächsten WLAN? -- was am besten ist.
- Cache-Eviction für selten genutzte Anhänge? -- nein
- „Anhang öffnen" via xdg-open (default app handler)? -- ja

App-Pfad:
- Bitte der standard-sailfishos pfad für die apps (bitte nochmal prüfen ob  ~/.local/share/harbour-imap-notes/harbour-imap-notes/notes.db

Apple-Header:
Damit iOS die Notiz erkennt, sollten mindestens diese geschrieben werden:
- X-Uniform-Type-Identifier: com.apple.mail-note
- X-Universally-Unique-Identifier: <UUID>
- X-Mail-Created-Date: <RFC2822-Date>
- Content-Type: text/html; charset=utf-8
- Mime-Version: 1.0
- eigenen X-Last-Modified für Konfliktlösung
- eigener X-ImapNotes-Format: plain|rich

Wo werden Sync-Fehler angezeigt?
- App-Cover: Letzter Sync vor X Min" + Status-Icon (✓/⚠/❌)?
- Status-Icon (✓/⚠/❌) in der App (z.B. in der Toolbar)

Design: 
- Versuche möglichst die nativen UX Elemente von Sailfish-OS zu verwenden (Pull-Down, Pull-Up, Swipe.) Ich möchte aber kein natives Notiz-App-Desing von SailfishOS (große Din-A4 Seiten pro Notiz) haben, sondern eine schlanke und übersichtliche Ordnerstruktur.
- Multi-Account-Switcher UI: Account-Auswahl als oberste Navigationsebene → dann Ordner darunter


1. iOS-Kompatibilität: Es soll keine iCloud Anbindung gemacht werden, sondern die Synchronisation soll über ein IMAP-Mailkonto erfolgen. iOS kann auch IMAP Mailkonten als Notizen einbinden, die Anbidung hier soll dem gleichen Prinzip folgen.
2. Technologie-Stack: 
   - IMAP-Client: Prüfe was am einfachsten und "nativsten" zu implementieren ist.
   - Lokaler Speicher: SQLITE Datenbank
   - Wo werden Credentials abgelegt? Prüfe hier die beste "nativste" Möglichkeit unter Sailfishos.
   - Soll die App Apple-Header schreiben: ja, die apple-spezifischen header sollen geschrieben werden. 
3. Rich-Text vs. Plaintext: die iOS Formatierungen sollen erhalten bleiben. Biete in den Optionen eine Option an, das Format der Notizen zu wählen (Rich-Text oder Plaintext - als eigener header X-ImapNotes-Format: plain|rich). Bei Format-Wechsel von Rich auf Plain soll eine Warnung kommen („iOS-Formatierung wird verloren gehen"). Bilder und Anhänge sollen unterstützt werden. die Einstellung plaintext/rich-text soll pro Notiz möglich sein.
4. Synchronisations-Strategie: Es soll grundsätzlich ein automatischer synch (z.B. alle 5 Minuten, Eigener systemd --user-Service) sein, falls das geht, sobald sich die notiz am server ändert direkt. Zusätzlich soll es noch einen manuellen sync-starten-button geben, falls der user zwischendrin synchronisieren möchte. Konfkliktbehandlung: last write wins und Edit-Wins-over-Delete, Eigener X-Last-Modified Header beim Schreiben, Vergleich darüber - offline-modus, sofern kein internet besteht. wird dann hochgeladen sobald internet wieder besteht. Wenn eine Notiz lokal oder remote gelöscht wird, am anderen gerät aber gleichzeitig nochmal editiert, wird die notiz wieder rüber synchronisiert. Erstelle dazu eine Tombstone-Tabelle mit Delete-Timestamp. Daemon hat IDLE und schreibt DB; App lauscht auf DBus-Signal imapnotes.sync.changed.
5. Ordner-Funktionalität: Verschachtelte Ordner wie in iOS, Ordner aus der App erstellen/umbenennen/löschen - Spezialbehandlung für „Auf meinem iPhone"-Ordner --> nein keine Spezialbehandlung nötig, Nur den Notes/-Subtree am Server lesen/schreiben (iOS-kompatibel, andere Mails bleiben unangetastet)
6. Account-Konfiguration: Mehrere IMAP accounts möglich, Multi-Account-UI: getrennt pro Account, keine vordefinierten profile, immer manuelle eingabe, TLS/STARTTLS erlauben, Integration mit SFOS-Accounts-System (/usr/share/accounts/providers) oder eigene Einstellungen --> Nur eigene einstellung. Account-Test-Button bei Einrichtung um IMAP Verbindung zu testen. Wenn kein Notes-Ordner angelegt ist, den Ordner automatisch erstellen (CREATE Notes)
7. UX-Details: Sortierung: zuletzt geändert ganz oben, Sortierung pro Ordner, suche über alle notizen eines accounts - ohne anhänge, volltextsuche über SQLITE -- ja, CoverPage: Letzter Synchronisationszeitpunkt anzeigen +  Quick-Actions („Neue Notiz", „Jetzt syncen") bei Quick action "Neue Notiz" - Default-Ordner für neue Notizen in den Einstellungen konfigurierbar, Pinned Notes --> JA, speichern Nur lokal in SQLite, Pinned Notes ganz oben als eigene sektion im ios style, Innerhalb Pinned: auch nach „zuletzt geändert" sortiert -- Onboarding: First-Run-Flow nicht nötig.
8. Sailfish-Spezifika: Zielversion SFOS mindestens 4.5 und höher.  Lizenz: Eclipse Public License - v 2.0 habe ich schon eingefügt, prüfe nochmal ob ich die richtige erwischt habe. Übersetzungen: Grundsätzliche Sprache, kommentare, Logs etc. alles in englisch. Eine übersetzungsdatei in Deutsch für den Anfang.

Entferntes feature: Locked Notes -- zu komplex, daher gestrichen