import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"

Page {
    id: page
    allowedOrientations: Orientation.All

    SilicaListView {
        id: list
        anchors.fill: parent
        model: accountsModel

        header: Column {
            width: list.width
            spacing: Theme.paddingMedium

            PageHeader {
                title: qsTr("Accounts")
                extraContent.children: [
                    SyncStatusIndicator {
                        anchors.verticalCenter: parent.verticalCenter
                    }
                ]
            }

            Label {
                visible: list.count === 0
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Pull down to add your first IMAP account")
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.Wrap
                width: parent.width - 2 * Theme.horizontalPageMargin
                horizontalAlignment: Text.AlignHCenter
            }
        }

        PullDownMenu {
            MenuItem {
                text: qsTr("Settings")
                onClicked: pageStack.animatorPush(Qt.resolvedUrl("SettingsPage.qml"))
            }
            MenuItem {
                text: qsTr("Sync all now")
                visible: list.count > 0
                onClicked: Sync.syncNow()
            }
            MenuItem {
                text: qsTr("Add account")
                onClicked: pageStack.animatorPush(Qt.resolvedUrl("AccountEditPage.qml"))
            }
        }

        delegate: ListItem {
            id: item
            contentHeight: row.implicitHeight + 2 * Theme.paddingMedium

            menu: ContextMenu {
                MenuItem {
                    text: qsTr("Edit account")
                    onClicked: pageStack.animatorPush(Qt.resolvedUrl("AccountEditPage.qml"),
                                                     { accountId: model.accountId })
                }
                MenuItem {
                    text: qsTr("Sync this account")
                    onClicked: Sync.syncAccount(model.accountId)
                }
                MenuItem {
                    text: qsTr("Remove account")
                    onClicked: Remorse.itemAction(item, qsTr("Removing"), function() {
                        Accounts.removeAccount(model.accountId)
                    })
                }
            }

            onClicked: pageStack.animatorPush(Qt.resolvedUrl("FoldersPage.qml"),
                                              { accountId: model.accountId,
                                                accountName: model.displayName })

            Row {
                id: row
                anchors {
                    left: parent.left
                    right: parent.right
                    verticalCenter: parent.verticalCenter
                    leftMargin: Theme.horizontalPageMargin
                    rightMargin: Theme.horizontalPageMargin
                }
                spacing: Theme.paddingMedium

                Column {
                    width: parent.width - statusLabel.implicitWidth - Theme.paddingMedium
                    spacing: Theme.paddingSmall / 2

                    Label {
                        text: model.displayName
                        font.pixelSize: Theme.fontSizeMedium
                        color: item.highlighted ? Theme.highlightColor : Theme.primaryColor
                        truncationMode: TruncationMode.Fade
                        width: parent.width
                    }

                    Label {
                        text: model.username + " · " + model.imapHost
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Theme.secondaryColor
                        truncationMode: TruncationMode.Fade
                        width: parent.width
                    }
                }

                Label {
                    id: statusLabel
                    anchors.verticalCenter: parent.verticalCenter
                    text: {
                        switch (model.syncStatus) {
                        case "ok": return "✓"
                        case "warning": return "⚠"
                        case "error": return "✗"
                        default: return "•"
                        }
                    }
                    color: model.syncStatus === "error" ? Theme.errorColor : Theme.highlightColor
                    font.pixelSize: Theme.fontSizeLarge
                }
            }
        }

        VerticalScrollDecorator {}
    }
}
