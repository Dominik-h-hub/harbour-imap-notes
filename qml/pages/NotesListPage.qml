import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"

Page {
    id: page
    allowedOrientations: Orientation.All

    property qint64 folderId: -1
    property string folderName
    property qint64 accountId: -1

    Component.onCompleted: notesModel.folderId = folderId

    SilicaListView {
        id: list
        anchors.fill: parent
        model: notesModel

        header: PageHeader {
            title: page.folderName.length ? page.folderName : qsTr("Notes")
        }

        PullDownMenu {
            MenuItem {
                text: qsTr("Search")
                onClicked: pageStack.animatorPush(Qt.resolvedUrl("SearchPage.qml"),
                                                  { accountId: page.accountId })
            }
            MenuItem {
                text: qsTr("Sync")
                onClicked: Sync.syncAccount(page.accountId)
            }
            MenuItem {
                text: qsTr("New note")
                onClicked: pageStack.animatorPush(Qt.resolvedUrl("NoteEditorPage.qml"),
                                                  { folderId: page.folderId })
            }
        }

        // The SQL orders pinned rows first; the QML section header just labels
        // the two groups visually.
        section {
            property: "pinned"
            criteria: ViewSection.FullString
            delegate: SectionHeader {
                text: section === "true" ? qsTr("Pinned") : qsTr("Notes")
            }
        }

        delegate: NoteListItem {
            id: noteItem
            width: list.width
            noteTitle: model.title
            preview: model.preview
            lastModified: model.lastModified
            pinned: model.pinned

            menu: ContextMenu {
                MenuItem {
                    text: model.pinned ? qsTr("Unpin") : qsTr("Pin to top")
                    onClicked: notesModel.togglePinned(model.noteId)
                }
                MenuItem {
                    text: qsTr("Move to trash")
                    onClicked: Remorse.itemAction(noteItem, qsTr("Deleting"),
                                                  function() { Notes.moveToTrash(model.noteId) })
                }
            }

            onClicked: pageStack.animatorPush(Qt.resolvedUrl("NoteEditorPage.qml"),
                                              { noteId: model.noteId })
        }

        ViewPlaceholder {
            enabled: list.count === 0
            text: qsTr("No notes yet")
            hintText: qsTr("Pull down to create one")
        }

        VerticalScrollDecorator {}
    }
}
