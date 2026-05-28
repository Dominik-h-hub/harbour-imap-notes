import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"

Page {
    id: page
    allowedOrientations: Orientation.All

    property var accountId: -1

    // The model picks up account scope through its folder filter. For a real
    // multi-account search we pass a sentinel folder belonging to this account
    // so the SQL constrains by account_id.
    SilicaListView {
        id: list
        anchors.fill: parent
        model: notesModel

        header: Column {
            width: list.width
            spacing: Theme.paddingMedium

            PageHeader {
                title: qsTr("Search")
            }

            SearchField {
                id: searchField
                width: parent.width
                placeholderText: qsTr("Search notes")
                onTextChanged: notesModel.searchQuery = text
            }
        }

        delegate: NoteListItem {
            width: list.width
            noteTitle: model.title
            preview: model.preview
            lastModified: model.lastModified
            pinned: model.pinned
            onClicked: pageStack.animatorPush(Qt.resolvedUrl("NoteEditorPage.qml"),
                                              { noteId: model.noteId })
        }

        ViewPlaceholder {
            enabled: list.count === 0 && searchField.text.length > 0
            text: qsTr("No matches")
        }

        VerticalScrollDecorator {}
    }

    Component.onDestruction: notesModel.searchQuery = ""
}
