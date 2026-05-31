import QtQuick 2.0
import Sailfish.Silica 1.0

// Compact toolbar above the note editor. Buttons emit signals; the editor
// page wires them up to either an HTML mutation helper (rich-text mode) or
// plain inserts (plaintext mode). The toolbar itself stays presentation-only.
Row {
    id: toolbar
    width: parent ? parent.width : Screen.width
    spacing: Theme.paddingSmall

    signal toggleBold()
    signal toggleItalic()
    signal toggleUnderline()
    signal toggleHeading()
    signal insertBullet()
    signal insertChecklist()
    signal attachFile()

    property bool richMode: true

    // Active-state flags — bound from the editor page.
    property bool boldActive:      false
    property bool italicActive:    false
    property bool underlineActive: false
    property bool headingActive:   false
    property bool bulletActive:    false

    // Calculate button size so all 7 buttons fit in the available width
    readonly property int _buttonCount: 7
    readonly property int _buttonSize: Math.max(
        Theme.iconSizeSmall,
        Math.floor((width - (_buttonCount - 1) * spacing) / _buttonCount)
    )

    Repeater {
        model: [
            { glyph: "B",  signalName: "toggleBold",      bold: true,      italic: false, underline: false },
            { glyph: "I",  signalName: "toggleItalic",    bold: false,     italic: true,  underline: false },
            { glyph: "U",  signalName: "toggleUnderline", bold: false,     italic: false, underline: true  },
            { glyph: "H",  signalName: "toggleHeading",   bold: true,      italic: false, underline: false },
            { glyph: "•",  signalName: "insertBullet",    bold: false,     italic: false, underline: false },
            { glyph: "☐",  signalName: "insertChecklist", bold: false,     italic: false, underline: false },
            { glyph: "📎", signalName: "attachFile",      bold: false,     italic: false, underline: false },
        ]
        delegate: Rectangle {
            id: btn
            width: toolbar._buttonSize
            height: toolbar._buttonSize
            radius: 4

            readonly property bool isEnabled: toolbar.richMode
                                              || modelData.signalName === "attachFile"
            readonly property bool isActive: {
                if (modelData.signalName === "toggleBold")      return toolbar.boldActive
                if (modelData.signalName === "toggleItalic")    return toolbar.italicActive
                if (modelData.signalName === "toggleUnderline") return toolbar.underlineActive
                if (modelData.signalName === "toggleHeading")   return toolbar.headingActive
                if (modelData.signalName === "insertBullet")    return toolbar.bulletActive
                return false
            }

            color: isActive
                   ? Theme.highlightBackgroundColor
                   : (btnArea.pressed ? Theme.highlightDimmerColor : "transparent")
            opacity: isEnabled ? 1.0 : 0.4

            Label {
                anchors.centerIn: parent
                text: modelData.glyph
                font.pixelSize: Theme.fontSizeSmall
                font.bold:      modelData.bold
                font.italic:    modelData.italic
                font.underline: modelData.underline
                color: (btn.isActive || btnArea.pressed)
                       ? Theme.highlightColor
                       : Theme.primaryColor
            }

            MouseArea {
                id: btnArea
                anchors.fill: parent
                enabled: btn.isEnabled
                onClicked: {
                    var sig = modelData.signalName
                    if      (sig === "toggleBold")      toolbar.toggleBold()
                    else if (sig === "toggleItalic")    toolbar.toggleItalic()
                    else if (sig === "toggleUnderline") toolbar.toggleUnderline()
                    else if (sig === "toggleHeading")   toolbar.toggleHeading()
                    else if (sig === "insertBullet")    toolbar.insertBullet()
                    else if (sig === "insertChecklist") toolbar.insertChecklist()
                    else if (sig === "attachFile")      toolbar.attachFile()
                }
            }
        }
    }
}
