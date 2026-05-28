import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    id: page
    allowedOrientations: Orientation.All

    property var accountId: -1
    property string accountName

    Component.onCompleted: foldersModel.accountId = accountId

    function openCreateDialog() {
        var dlg = pageStack.push(Qt.resolvedUrl("FolderEditDialog.qml"),
                                 { dialogTitle: qsTr("New folder") })
        dlg.accepted.connect(function() {
            Notes.createFolder(page.accountId, "", dlg.folderName)
        })
    }

    function openRenameDialog(folderId, currentName) {
        var dlg = pageStack.push(Qt.resolvedUrl("FolderEditDialog.qml"),
                                 { dialogTitle: qsTr("Rename folder"),
                                   initialName: currentName })
        dlg.accepted.connect(function() {
            Notes.renameFolder(folderId, dlg.folderName)
        })
    }

    SilicaListView {
        id: list
        anchors.fill: parent
        model: foldersModel

        header: PageHeader {
            title: page.accountName.length ? page.accountName : qsTr("Folders")
        }

        PullDownMenu {
            MenuItem {
                text: qsTr("Search this account")
                onClicked: pageStack.animatorPush(Qt.resolvedUrl("SearchPage.qml"),
                                                  { accountId: page.accountId })
            }
            MenuItem {
                text: qsTr("Recently deleted")
                onClicked: pageStack.animatorPush(Qt.resolvedUrl("TrashPage.qml"),
                                                  { accountId: page.accountId })
            }
            MenuItem {
                text: qsTr("Sync this account")
                onClicked: Sync.syncAccount(page.accountId)
            }
            MenuItem {
                text: qsTr("New folder")
                onClicked: page.openCreateDialog()
            }
        }

        delegate: ListItem {
            id: item
            contentHeight: Theme.itemSizeMedium

            visible: !model.isTrash
            height: visible ? contentHeight : 0

            onClicked: pageStack.animatorPush(Qt.resolvedUrl("NotesListPage.qml"),
                                              { folderId: model.folderId,
                                                folderName: model.displayName,
                                                accountId: page.accountId })

            menu: ContextMenu {
                MenuItem {
                    text: qsTr("Rename folder")
                    onClicked: page.openRenameDialog(model.folderId, model.displayName)
                }
                MenuItem {
                    text: qsTr("Delete folder")
                    onClicked: item.remorseAction(qsTr("Deleting"),
                                                  function() { Notes.deleteFolder(model.folderId) })
                }
            }

            Row {
                anchors {
                    left: parent.left
                    right: parent.right
                    verticalCenter: parent.verticalCenter
                    leftMargin: Theme.horizontalPageMargin
                    rightMargin: Theme.horizontalPageMargin
                }
                spacing: Theme.paddingMedium

                Label {
                    text: "📁"
                    font.pixelSize: Theme.fontSizeMedium
                    anchors.verticalCenter: parent.verticalCenter
                }

                Label {
                    text: model.displayName
                    font.pixelSize: Theme.fontSizeMedium
                    color: item.highlighted ? Theme.highlightColor : Theme.primaryColor
                    truncationMode: TruncationMode.Fade
                    width: parent.width - Theme.iconSizeMedium - countLabel.implicitWidth - 2 * Theme.paddingMedium
                    anchors.verticalCenter: parent.verticalCenter
                }

                Label {
                    id: countLabel
                    text: model.noteCount
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }

        ViewPlaceholder {
            enabled: list.count === 0
            text: qsTr("No folders yet")
            hintText: qsTr("Pull down to sync or create one")
        }

        VerticalScrollDecorator {}
    }
}
