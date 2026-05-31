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

    // Active-format toggle state — true means "next typed chars use this style".
    // Buttons show as highlighted when the corresponding flag is true.
    // Reset whenever the user taps into a (different) TextEdit block.
    property bool formatBold:      false
    property bool formatItalic:    false
    property bool formatUnderline: false
    property bool formatHeading:   false
    property bool formatBullet:    false

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

    // Apply / remove an inline HTML tag (b / i / u).
    //
    // State is tracked via page.formatBold / formatItalic / formatUnderline.
    // First press: activates the format (button highlights, next typed chars are
    //              styled).
    // Second press: deactivates — inserts a zero-width space with an explicit
    //              "reset" span so the cursor inherits the normal style.
    // With a selection: wraps (or unwraps) the selected text; flag state toggles.
    function applyInlineFormat(tag) {
        if (!focusedTextArea) {
            statusLabel.text = qsTr("Tap into a text area first.")
            return
        }
        var ed = focusedTextArea

        // Read current state and toggle it.
        var wasActive
        var normalStyle
        if (tag === "b") {
            wasActive = page.formatBold
            page.formatBold = !wasActive
            normalStyle = "font-weight:normal"
        } else if (tag === "i") {
            wasActive = page.formatItalic
            page.formatItalic = !wasActive
            normalStyle = "font-style:normal"
        } else if (tag === "u") {
            wasActive = page.formatUnderline
            page.formatUnderline = !wasActive
            normalStyle = "text-decoration:none"
        } else {
            wasActive = false
            normalStyle = ""
        }

        var s = ed.selectionStart
        var e = ed.selectionEnd
        if (s < e) {
            var sel = ed.selectedText
            ed.remove(s, e)
            if (!wasActive) {
                ed.insert(s, "<" + tag + ">" + sel + "</" + tag + ">")
            } else {
                // Strip the tag — re-insert as plain text.
                ed.insert(s, sel)
            }
            ed.cursorPosition = s + sel.length
        } else {
            var pos = ed.cursorPosition
            if (!wasActive) {
                // Activate: anchor a bold/italic/underline zero-width space;
                // cursor at pos+1 inherits that character's format.
                ed.insert(pos, "<" + tag + ">\u200B</" + tag + ">")
            } else {
                // Deactivate: anchor an explicitly reset zero-width space;
                // cursor at pos+1 inherits the "normal" style.
                ed.insert(pos, "<span style=\"" + normalStyle + "\">\u200B</span>")
            }
            ed.cursorPosition = pos + 1
        }
        ed.forceActiveFocus()
        dirty = true
    }

    // Wrap selected text in an <h2>, or insert an <h2> block with a
    // zero-width-space placeholder so the cursor lands inside the heading.
    // Second press resets to normal paragraph style.
    function applyHeading() {
        if (!focusedTextArea) {
            statusLabel.text = qsTr("Tap into a text area first.")
            return
        }
        var ed = focusedTextArea
        var wasActive = page.formatHeading
        page.formatHeading = !wasActive

        var s = ed.selectionStart
        var e = ed.selectionEnd
        if (s < e) {
            var sel = ed.selectedText
            ed.remove(s, e)
            if (!wasActive) {
                ed.insert(s, "<h2>" + sel + "</h2>")
            } else {
                // Remove heading — re-insert as plain text
                ed.insert(s, sel)
            }
            ed.cursorPosition = s + sel.length
        } else {
            var pos = ed.cursorPosition
            if (!wasActive) {
                // Activate heading: anchor a h2 zero-width space; cursor inherits heading style
                ed.insert(pos, "<h2>\u200B</h2>")
            } else {
                // Deactivate: anchor a normal-style zero-width space to break out of heading
                ed.insert(pos, "<span style=\"font-size:medium;font-weight:normal\">\u200B</span>")
            }
            ed.cursorPosition = pos + 1
        }
        ed.forceActiveFocus()
        dirty = true
    }

    // Insert an arbitrary HTML snippet (bullet list, etc.) at the cursor.
    function insertHtmlAtCursor(snippet) {
        if (!focusedTextArea) {
            statusLabel.text = qsTr("Tap into a text area first.")
            return
        }
        var ed = focusedTextArea
        var pos = ed.cursorPosition
        ed.insert(pos, snippet)
        ed.forceActiveFocus()
        dirty = true
    }

    // Toggle bullet-list mode. First press inserts a <ul><li> and lights up
    // the button; second press inserts a normal-style span to break out of
    // the list, and the button goes dark again.
    function applyBullet() {
        if (!focusedTextArea) {
            statusLabel.text = qsTr("Tap into a text area first.")
            return
        }
        var ed = focusedTextArea
        var wasActive = page.formatBullet
        page.formatBullet = !wasActive
        var pos = ed.cursorPosition
        if (!wasActive) {
            // Activate: start a bullet list; cursor lands inside the first <li>
            ed.insert(pos, "<ul><li>\u200B</li></ul>")
        } else {
            // Deactivate: break out of the list with a plain-style anchor
            ed.insert(pos, "<span style=\"font-size:small;font-weight:normal\">\u200B</span>")
        }
        ed.cursorPosition = pos + 1
        ed.forceActiveFocus()
        dirty = true
    }

    Component.onCompleted: loadNote()
    Component.onDestruction: saveNote()

    // ── Delegate components ─────────────────────────────────────────────
    Component {
        id: textBlockComp
        // Use QML's native TextEdit directly so textFormat, insert(),
        // selectionStart/End etc. are all accessible without private _editor hacks.
        TextEdit {
            id: textBlock
            property int blockIndex: -1
            // Guard flag: suppress onTextChanged side-effects while loading
            property bool _loading: false

            width: column.width
            textFormat: TextEdit.RichText
            wrapMode: TextEdit.Wrap
            color: Theme.primaryColor
            selectionColor: Theme.highlightBackgroundColor
            selectedTextColor: Theme.highlightColor
            font.pixelSize: Theme.fontSizeSmall
            cursorVisible: activeFocus

            // Placeholder text — shown when the block is empty
            Text {
                anchors.fill: parent
                visible: textBlock.text.length === 0 && !textBlock.activeFocus
                         && textBlock.blockIndex === 0 && page.blocks.length === 1
                text: qsTr("Start typing…")
                color: Theme.secondaryColor
                font: textBlock.font
            }

            onTextChanged: {
                if (_loading) return
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
                    // Reset format-toggle state when the user taps into a new
                    // block — we don't know the character format at the new
                    // cursor position, so start clean.
                    page.formatBold      = false
                    page.formatItalic    = false
                    page.formatUnderline = false
                    page.formatHeading   = false
                    page.formatBullet    = false
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
                boldActive:      page.formatBold
                italicActive:    page.formatItalic
                underlineActive: page.formatUnderline
                headingActive:   page.formatHeading
                bulletActive:    page.formatBullet
                onToggleBold:      page.applyInlineFormat("b")
                onToggleItalic:    page.applyInlineFormat("i")
                onToggleUnderline: page.applyInlineFormat("u")
                onToggleHeading:   page.applyHeading()
                onInsertBullet:    page.applyBullet()
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
                            item._loading = true
                            item.blockIndex = index
                            if (modelData.type === "checklist") {
                                item.initialItems = modelData.items || []
                            } else {
                                item.text = modelData.html || ""
                            }
                            item._loading = false
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
