import QtQuick 2.0
import Sailfish.Silica 1.0

Row {
    id: indicator
    spacing: Theme.paddingSmall

    property string status: Sync.status
    property string lastError: Sync.lastError
    property var lastSync: Sync.lastSync

    Label {
        anchors.verticalCenter: parent.verticalCenter
        font.pixelSize: Theme.fontSizeExtraSmall
        color: indicator.status === "error" ? Theme.errorColor : Theme.secondaryColor
        text: {
            switch (indicator.status) {
            case "ok": return "✓"
            case "warning": return "⚠"
            case "error": return "✗"
            default: return "•"
            }
        }
    }

    Label {
        anchors.verticalCenter: parent.verticalCenter
        font.pixelSize: Theme.fontSizeExtraSmall
        color: Theme.secondaryColor
        text: indicator.lastSync && indicator.lastSync.toString()
              ? Qt.formatDateTime(indicator.lastSync, "dd.MM. hh:mm")
              : qsTr("never")
    }
}
