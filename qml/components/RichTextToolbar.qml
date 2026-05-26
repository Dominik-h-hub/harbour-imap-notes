import QtQuick 2.0
import Sailfish.Silica 1.0

// Compact toolbar above the note editor. Buttons emit signals; the editor
// page wires them up to either an HTML mutation helper (rich-text mode) or
// plain inserts (plaintext mode). The toolbar itself stays presentation-only.
Row {
    id: toolbar
    spacing: Theme.paddingSmall

    signal toggleBold()
    signal toggleItalic()
    signal toggleUnderline()
    signal toggleHeading()
    signal insertBullet()
    signal insertCheckbox()
    signal attachFile()

    property bool richMode: true

    Repeater {
        model: [
            { glyph: "B",  signalName: "toggleBold",      bold: true },
            { glyph: "I",  signalName: "toggleItalic",    italic: true },
            { glyph: "U",  signalName: "toggleUnderline", underline: true },
            { glyph: "H",  signalName: "toggleHeading",   bold: true },
            { glyph: "•",  signalName: "insertBullet" },
            { glyph: "☐",  signalName: "insertCheckbox" },
            { glyph: "📎", signalName: "attachFile" },
        ]
        delegate: IconButton {
            width: Theme.iconSizeMedium
            height: Theme.iconSizeMedium
            enabled: toolbar.richMode || modelData.signalName === "attachFile"
            opacity: enabled ? 1.0 : 0.4

            Label {
                anchors.centerIn: parent
                text: modelData.glyph
                font.pixelSize: Theme.fontSizeSmall
                font.bold: modelData.bold === true
                font.italic: modelData.italic === true
                font.underline: modelData.underline === true
                color: Theme.primaryColor
            }

            onClicked: {
                switch (modelData.signalName) {
                case "toggleBold":      toolbar.toggleBold(); break
                case "toggleItalic":    toolbar.toggleItalic(); break
                case "toggleUnderline": toolbar.toggleUnderline(); break
                case "toggleHeading":   toolbar.toggleHeading(); break
                case "insertBullet":    toolbar.insertBullet(); break
                case "insertCheckbox":  toolbar.insertCheckbox(); break
                case "attachFile":      toolbar.attachFile(); break
                }
            }
        }
    }
}
