import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page
    allowedOrientations: Orientation.All

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.implicitHeight

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingMedium

            PageHeader {
                title: qsTr("Settings")
            }

            SectionHeader { text: qsTr("Synchronisation") }

            ComboBox {
                width: parent.width
                label: qsTr("Sync interval")
                currentIndex: {
                    var minutes = AppSettings.syncIntervalMinutes
                    var values = [1, 5, 15, 30, 60]
                    var idx = values.indexOf(minutes)
                    return idx < 0 ? 1 : idx
                }
                menu: ContextMenu {
                    MenuItem { text: qsTr("Every minute") }
                    MenuItem { text: qsTr("Every 5 minutes") }
                    MenuItem { text: qsTr("Every 15 minutes") }
                    MenuItem { text: qsTr("Every 30 minutes") }
                    MenuItem { text: qsTr("Every hour") }
                }
                onCurrentIndexChanged: {
                    var values = [1, 5, 15, 30, 60]
                    AppSettings.syncIntervalMinutes = values[currentIndex]
                }
            }

            TextSwitch {
                width: parent.width
                text: qsTr("Sync attachments over mobile data")
                description: qsTr("When off, attachments only sync on Wi-Fi.")
                checked: AppSettings.syncAttachmentsOverMobile
                onCheckedChanged: AppSettings.syncAttachmentsOverMobile = checked
            }

            SectionHeader { text: qsTr("Trash") }

            ComboBox {
                width: parent.width
                label: qsTr("Auto-delete trash after")
                currentIndex: {
                    var days = AppSettings.trashCleanupDays
                    var values = [7, 30, 90, 365, 0]
                    var idx = values.indexOf(days)
                    return idx < 0 ? 1 : idx
                }
                menu: ContextMenu {
                    MenuItem { text: qsTr("7 days") }
                    MenuItem { text: qsTr("30 days") }
                    MenuItem { text: qsTr("90 days") }
                    MenuItem { text: qsTr("1 year") }
                    MenuItem { text: qsTr("Never") }
                }
                onCurrentIndexChanged: {
                    var values = [7, 30, 90, 365, 0]
                    AppSettings.trashCleanupDays = values[currentIndex]
                }
            }

            SectionHeader { text: qsTr("New notes") }

            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                text: AppSettings.defaultFolderId < 0
                      ? qsTr("No default folder set — new notes prompt for a folder.")
                      : qsTr("Default folder configured. Edit the folder list to change.")
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeExtraSmall
            }
        }
    }
}
