import QtQuick 2.0
import Sailfish.Silica 1.0
import harbour.imapnotes 1.0
import "../components"

Page {
    id: page
    allowedOrientations: Orientation.All

    property var noteId: -1
    property var folderId: -1
    property string format: "rich"
    property bool dirty: false
    readonly property bool richMode: format === "rich"

    function loadNote() {
        if (noteId < 0) return
        var n = Notes.note(noteId)
        if (!n || Object.keys(n).length === 0) return
        titleField.text = n.title || ""
        bodyArea.text = page.richMode ? (n.bodyHtml || "")
                                       : RichText.htmlToPlain(n.bodyHtml || "")
        format = n.format || "rich"
        folderId = n.folderId
        dirty = false
    }

    function saveNote() {
        if (!dirty) return
        var body = page.richMode ? bodyArea.text : RichText.plainToHtml(bodyArea.text)
        if (noteId < 0) {
            if (folderId < 0) {
                folderId = AppSettings.defaultFolderId
            }
            if (folderId < 0) {
                // No folder picked yet — show an inline hint instead of
                // silently dropping the edit.
                statusLabel.text = qsTr("Pick a folder via Settings → Default folder before saving.")
                return
            }
            noteId = Notes.createNote(folderId, titleField.text, body, format)
        } else {
            Notes.updateNote(noteId, titleField.text, body, format)
        }
        dirty = false
    }

    Component.onCompleted: loadNote()
    Component.onDestruction: saveNote()

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.implicitHeight + 2 * Theme.paddingLarge

        PullDownMenu {
            MenuItem {
                text: qsTr("Save")
                enabled: page.dirty
                onClicked: page.saveNote()
            }
            MenuItem {
                text: page.richMode ? qsTr("Switch to plain text")
                                    : qsTr("Switch to rich text")
                onClicked: {
                    if (page.richMode) {
                        formatWarningDialog.open()
                    } else {
                        page.format = "rich"
                        page.dirty = true
                    }
                }
            }
            MenuItem {
                text: qsTr("Attach file")
                onClicked: statusLabel.text =
                    qsTr("Attachment picker wires up in a follow-up.")
            }
        }

        Column {
            id: column
            anchors {
                left: parent.left
                right: parent.right
                leftMargin: Theme.horizontalPageMargin
                rightMargin: Theme.horizontalPageMargin
            }
            spacing: Theme.paddingMedium

            PageHeader {
                title: page.noteId < 0 ? qsTr("New note") : qsTr("Edit note")
            }

            TextField {
                id: titleField
                width: parent.width
                placeholderText: qsTr("Title")
                font.pixelSize: Theme.fontSizeLarge
                onTextChanged: page.dirty = true
            }

            RichTextToolbar {
                richMode: page.richMode
                onToggleBold: { bodyArea.text += "<b></b>"; page.dirty = true }
                onToggleItalic: { bodyArea.text += "<i></i>"; page.dirty = true }
                onToggleUnderline: { bodyArea.text += "<u></u>"; page.dirty = true }
                onToggleHeading: { bodyArea.text += "<h2></h2>"; page.dirty = true }
                onInsertBullet: { bodyArea.text += "<ul><li></li></ul>"; page.dirty = true }
                onInsertCheckbox: { bodyArea.text += "☐ "; page.dirty = true }
                onAttachFile: statusLabel.text = qsTr("Attachment picker wires up in a follow-up.")
            }

            TextArea {
                id: bodyArea
                width: parent.width
                placeholderText: qsTr("Start typing…")
                wrapMode: TextEdit.Wrap
                onTextChanged: page.dirty = true

                Binding {
                    target: bodyArea._editor
                    property: "textFormat"
                    value: page.richMode ? TextEdit.RichText : TextEdit.PlainText
                }
            }

            Label {
                id: statusLabel
                width: parent.width
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeExtraSmall
            }
        }
    }

    Dialog {
        id: formatWarningDialog
        DialogHeader {
            acceptText: qsTr("Switch")
            cancelText: qsTr("Keep rich")
        }
        Column {
            width: parent.width - 2 * Theme.horizontalPageMargin
            x: Theme.horizontalPageMargin
            spacing: Theme.paddingLarge

            Label {
                width: parent.width
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeMedium
                text: qsTr("Switching to plain text will drop bold, italic, headings and other formatting. iOS edits sent later will arrive as plain text too.")
            }
        }
        onAccepted: {
            // Convert current rich content to plain before flipping the mode
            // so the editor doesn't show raw HTML.
            bodyArea.text = RichText.htmlToPlain(bodyArea.text)
            page.format = "plain"
            page.dirty = true
        }
    }
}
