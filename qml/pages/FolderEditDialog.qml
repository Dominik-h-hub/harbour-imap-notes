import QtQuick 2.0
import Sailfish.Silica 1.0

// Minimal name-only dialog used for both Create and Rename. The caller
// supplies the title and an accept handler; this page just collects the new
// display name and exposes it on accept.
Dialog {
    id: dialog
    allowedOrientations: Orientation.All

    property string dialogTitle: qsTr("Folder")
    property string initialName

    canAccept: nameField.text.trim().length > 0

    property string folderName: nameField.text.trim()

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.implicitHeight

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingMedium

            DialogHeader {
                title: dialog.dialogTitle
                acceptText: qsTr("OK")
            }

            TextField {
                id: nameField
                width: parent.width
                label: qsTr("Folder name")
                text: dialog.initialName
                focus: true
            }
        }
    }
}
