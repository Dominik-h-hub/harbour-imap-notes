import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"

Page {
    id: page
    allowedOrientations: Orientation.All

    property var accountId: -1

    Component.onCompleted: {
        var trashId = Notes.trashFolderIdFor(accountId)
        if (trashId >= 0) {
            notesModel.folderId = trashId
        }
    }

    SilicaListView {
        id: list
        anchors.fill: parent
        model: notesModel

        header: PageHeader {
            title: qsTr("Recently deleted")
        }

        delegate: NoteListItem {
            id: trashItem
            width: list.width
            noteTitle: model.title
            preview: model.preview
            lastModified: model.lastModified
            pinned: false

            menu: ContextMenu {
                MenuItem {
                    text: qsTr("Restore")
                    onClicked: Notes.restoreFromTrash(model.noteId)
                }
                MenuItem {
                    text: qsTr("Delete forever")
                    onClicked: trashItem.remorseAction(qsTr("Deleting"),
                                                       function() { Notes.deleteNotePermanently(model.noteId) })
                }
            }

            onClicked: pageStack.animatorPush(Qt.resolvedUrl("NoteEditorPage.qml"),
                                              { noteId: model.noteId })
        }

        ViewPlaceholder {
            enabled: list.count === 0
            text: qsTr("Trash is empty")
            hintText: qsTr("Notes you delete will appear here for 30 days")
        }

        VerticalScrollDecorator {}
    }
}
