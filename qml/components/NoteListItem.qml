import QtQuick 2.0
import Sailfish.Silica 1.0

ListItem {
    id: item
    contentHeight: column.implicitHeight + 2 * Theme.paddingMedium

    property string noteTitle
    property string preview
    property var lastModified
    property bool pinned

    Column {
        id: column
        anchors {
            left: parent.left
            right: parent.right
            verticalCenter: parent.verticalCenter
            leftMargin: Theme.horizontalPageMargin
            rightMargin: Theme.horizontalPageMargin
        }
        spacing: Theme.paddingSmall

        Row {
            spacing: Theme.paddingSmall
            width: parent.width

            Label {
                visible: item.pinned
                text: "📌"
                font.pixelSize: Theme.fontSizeSmall
                anchors.verticalCenter: parent.verticalCenter
            }

            Label {
                text: item.noteTitle.length ? item.noteTitle : qsTr("(no title)")
                font.pixelSize: Theme.fontSizeMedium
                color: item.highlighted ? Theme.highlightColor : Theme.primaryColor
                truncationMode: TruncationMode.Fade
                width: parent.width - (item.pinned ? Theme.itemSizeExtraSmall : 0)
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        Label {
            text: item.preview
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryColor
            truncationMode: TruncationMode.Fade
            width: parent.width
            visible: text.length > 0
        }

        Label {
            text: item.lastModified
                  ? new Date(item.lastModified * 1000).toLocaleString(Qt.locale(), Locale.ShortFormat)
                  : ""
            font.pixelSize: Theme.fontSizeTiny
            color: Theme.secondaryColor
            visible: text.length > 0
        }
    }
}
