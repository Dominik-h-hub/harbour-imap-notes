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

    // Block-based source of truth for rich mode. Updated by the delegate
    // signal handlers; reassigned (sliced) when blocks are added/removed/
    // reordered so the Repeater re-instantiates delegates.
    property var blocks: [ { type: "text", html: "" } ]

    // Tracks which TextArea is currently focused so the toolbar can insert
    // formatting at the right cursor position.
    property var focusedTextArea: null
    property int focusedBlockIndex: -1

    function _refresh() {
        // Reassign a sliced copy so the Repeater notices the structural change.
        blocks = blocks.slice()
    }

    function loadNote() {
        if (noteId < 0) return
        var n = Notes.note(noteId)
        if (!n || Object.keys(n).length === 0) return
        titleField.text = n.title || ""
        format = n.format || "rich"
        folderId = n.folderId
        if (richMode) {
            var parsed = Checklist.parseBlocks(n.bodyHtml || "")
            blocks = parsed.length > 0 ? parsed : [ { type: "text", html: "" } ]
        } else {
            plainArea.text = RichText.htmlToPlain(n.bodyHtml || "")
        }
        dirty = false
    }

    function saveNote() {
        if (!dirty) return
        var body
        if (richMode) {
            body = Checklist.serializeBlocks(blocks, true)
        } else {
            body = RichText.plainToHtml(plainArea.text)
        }
        if (noteId < 0) {
            if (folderId < 0) {
                folderId = AppSettings.defaultFolderId
            }
            if (folderId < 0) {
                statusLabel.text = qsTr("Pick a folder via Settings → Default folder before saving.")
                return
            }
            noteId = Notes.createNote(folderId, titleField.text, body, format)
        } else {
            Notes.updateNote(noteId, titleField.text, body, format)
        }
        dirty = false
    }

    // ── Block mutations ────────────────────────────────────────────────
    function insertChecklistAfter(idx) {
        var newBlocks = blocks.slice(0, idx + 1)
        newBlocks.push({ type: "checklist", items: [ { text: "", done: false } ] })
        for (var i = idx + 1; i < blocks.length; i++) newBlocks.push(blocks[i])
        blocks = newBlocks
        dirty = true
    }

    function endChecklistAt(idx) {
        var newBlocks = blocks.slice(0, idx + 1)
        newBlocks.push({ type: "text", html: "" })
        for (var i = idx + 1; i < blocks.length; i++) newBlocks.push(blocks[i])
        blocks = newBlocks
        dirty = true
    }

    function removeBlockAt(idx) {
        var newBlocks = []
        for (var i = 0; i < blocks.length; i++) {
            if (i === idx) continue
            newBlocks.push(blocks[i])
        }
        if (newBlocks.length === 0) {
            newBlocks.push({ type: "text", html: "" })
        }
        blocks = newBlocks
        dirty = true
    }

    function insertHtmlAtCursor(snippet) {
        if (!focusedTextArea) {
            statusLabel.text = qsTr("Tap into a text area first.")
            return
        }
        // Silica's TextArea wraps a private TextEdit (_editor). The wrapper
        // doesn't forward insert(); we have to call it on the inner editor.
        // In RichText mode the snippet is parsed as HTML; in PlainText mode
        // it's inserted literally (but the toolbar is disabled there anyway).
        var editor = focusedTextArea._editor
        if (editor && typeof editor.insert === "function") {
            editor.insert(editor.cursorPosition, snippet)
        } else {
            // Defensive fallback: splice into the text property. Loses the
            // caret position but won't crash on a Silica build that hides
            // _editor.
            var pos = focusedTextArea.cursorPosition
            var t = focusedTextArea.text
            focusedTextArea.text = t.substring(0, pos) + snippet + t.substring(pos)
        }
        dirty = true
    }

    Component.onCompleted: loadNote()
    Component.onDestruction: saveNote()

    // ── Delegate components ─────────────────────────────────────────────
    Component {
        id: textBlockComp
        TextArea {
            id: textBlock
            property int blockIndex: -1
            width: column.width
            placeholderText: blockIndex === 0 && page.blocks.length === 1
                             ? qsTr("Start typing…") : ""
            wrapMode: TextEdit.Wrap

            // Silica's TextArea wraps a private TextEdit (_editor) — the
            // public `textFormat` alias isn't always honoured, so we bind
            // through to the inner editor like the previous editor did.
            Binding {
                target: textBlock._editor
                property: "textFormat"
                value: page.richMode ? TextEdit.RichText : TextEdit.PlainText
            }

            onTextChanged: {
                if (blockIndex < 0 || blockIndex >= page.blocks.length) return
                if (page.blocks[blockIndex].type !== "text") return
                if (page.blocks[blockIndex].html === text) return
                page.blocks[blockIndex].html = text
                page.dirty = true
            }
            onActiveFocusChanged: {
                if (activeFocus) {
                    page.focusedTextArea = textBlock
                    page.focusedBlockIndex = blockIndex
                }
            }
        }
    }

    Component {
        id: checklistBlockComp
        ChecklistBlock {
            id: checklistBlock
            width: column.width
            richMode: page.richMode
            onContentChanged: {
                if (blockIndex < 0 || blockIndex >= page.blocks.length) return
                page.blocks[blockIndex].items = checklistBlock.currentItems()
                page.dirty = true
            }
            onRequestEndList: page.endChecklistAt(idx)
            onRequestRemoveBlock: page.removeBlockAt(idx)
        }
    }

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
                        // Plain → Rich: re-parse glyphs back into structured
                        // checklists. Done silently per requirements §5.4.
                        page.blocks = Checklist.plaintextToBlocks(plainArea.text)
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
                onToggleBold: page.insertHtmlAtCursor("<b></b>")
                onToggleItalic: page.insertHtmlAtCursor("<i></i>")
                onToggleUnderline: page.insertHtmlAtCursor("<u></u>")
                onToggleHeading: page.insertHtmlAtCursor("<h2></h2>")
                onInsertBullet: page.insertHtmlAtCursor("<ul><li></li></ul>")
                onInsertChecklist: {
                    var idx = page.focusedBlockIndex
                    if (idx < 0) idx = page.blocks.length - 1
                    page.insertChecklistAfter(idx)
                }
                onAttachFile: statusLabel.text =
                    qsTr("Attachment picker wires up in a follow-up.")
            }

            // ── Rich-mode block stack ───────────────────────────────────
            Column {
                id: blockStack
                width: parent.width
                spacing: Theme.paddingMedium
                visible: page.richMode

                Repeater {
                    id: blocksRepeater
                    model: page.blocks
                    delegate: Loader {
                        width: blockStack.width
                        sourceComponent: modelData && modelData.type === "checklist"
                                         ? checklistBlockComp : textBlockComp
                        onLoaded: {
                            item.blockIndex = index
                            if (modelData.type === "checklist") {
                                item.initialItems = modelData.items || []
                            } else {
                                item.text = modelData.html || ""
                            }
                        }
                    }
                }
            }

            // ── Plain-mode single TextArea ──────────────────────────────
            TextArea {
                id: plainArea
                width: parent.width
                visible: !page.richMode
                placeholderText: qsTr("Start typing…")
                wrapMode: TextEdit.Wrap

                Binding {
                    target: plainArea._editor
                    property: "textFormat"
                    value: TextEdit.PlainText
                }

                onTextChanged: {
                    if (!page.richMode) page.dirty = true
                }
                onActiveFocusChanged: {
                    if (activeFocus) {
                        page.focusedTextArea = plainArea
                        page.focusedBlockIndex = -1
                    }
                }
            }

            Label {
                id: statusLabel
                width: parent.width
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeExtraSmall
            }

            // Tiny hint shown the first time a note contains a checklist —
            // explains the iOS-side read-only trade-off per requirements §5.3.
            Label {
                width: parent.width
                wrapMode: Text.Wrap
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeExtraSmall
                visible: page.richMode && hasChecklist()
                text: qsTr("Checklists show on iOS as a bullet list with ☐/☑ — taps to toggle only work here on Sailfish.")

                function hasChecklist() {
                    for (var i = 0; i < page.blocks.length; i++) {
                        if (page.blocks[i].type === "checklist") return true
                    }
                    return false
                }
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
                text: qsTr("Switching to plain text will drop bold, italic, headings and other formatting. Checklists become ☐/☑ text lines. iOS edits sent later will arrive as plain text too.")
            }
        }
        onAccepted: {
            // Rich → Plain: render blocks (including checklist glyphs) into
            // the plaintext area, then flip mode.
            plainArea.text = Checklist.plaintextFromBlocks(page.blocks)
            page.format = "plain"
            page.dirty = true
        }
    }
}
