import QtQuick 2.0
import Sailfish.Silica 1.0

CoverBackground {
    Column {
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.paddingMedium
        spacing: Theme.paddingMedium

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("IMAP Notes")
            font.pixelSize: Theme.fontSizeLarge
            color: Theme.primaryColor
        }

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: {
                if (!Sync.lastSync) return qsTr("Never synced")
                var d = Sync.lastSync
                if (typeof d.getTime === "function" && isNaN(d.getTime())) {
                    return qsTr("Never synced")
                }
                return d.toLocaleString(Qt.locale(), Locale.ShortFormat)
            }
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryColor
            wrapMode: Text.Wrap
        }

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: {
                switch (Sync.status) {
                case "ok": return "✓ " + qsTr("Up to date")
                case "warning": return "⚠ " + qsTr("Sync warning")
                case "error": return "✗ " + (Sync.lastError || qsTr("Sync error"))
                case "syncing": return qsTr("Syncing…")
                default: return qsTr("Idle")
                }
            }
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Sync.status === "error" ? Theme.errorColor : Theme.highlightColor
            wrapMode: Text.Wrap
        }
    }

    // Single cover action: trigger sync from the cover. New-note from the
    // cover would require ApplicationWindow.activate() + a deferred push
    // through the pageStack, which is not reliable from cover scope —
    // deferring that until we wire it through DBus.
    CoverActionList {
        CoverAction {
            iconSource: "image://theme/icon-cover-sync"
            onTriggered: Sync.syncNow()
        }
    }
}
