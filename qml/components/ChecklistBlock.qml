import QtQuick 2.0
import Sailfish.Silica 1.0

// A flat checklist block for the mixed-content note editor. Source of truth is
// the internal `itemsModel` ListModel — the editor reads it back via
// currentItems() at save time and re-assigns the parent block's `items` array.
//
// On-disk format is rendered by ChecklistParser::serializeBlocks; this widget
// only deals with the in-memory representation.
Item {
    id: block

    // Set once from the editor when the delegate is loaded:
    property var initialItems: []
    property int blockIndex: -1
    property bool richMode: true

    // Notify the editor when something changes so it can mark the page dirty.
    signal contentChanged()
    // Emitted when the user pressed Enter on an empty item; the editor reacts
    // by closing this checklist and opening a new text block after it.
    signal requestEndList(int idx)
    // Emitted when the user removes the last remaining item with backspace
    // — the editor then drops the whole block.
    signal requestRemoveBlock(int idx)

    width: parent ? parent.width : 0
    implicitHeight: column.implicitHeight

    ListModel { id: itemsModel }

    Component.onCompleted: {
        itemsModel.clear()
        for (var i = 0; i < initialItems.length; i++) {
            var it = initialItems[i]
            itemsModel.append({ text: it.text || "", done: it.done === true })
        }
        if (itemsModel.count === 0) {
            itemsModel.append({ text: "", done: false })
        }
    }

    function currentItems() {
        var arr = []
        for (var i = 0; i < itemsModel.count; i++) {
            var it = itemsModel.get(i)
            arr.push({ text: it.text, done: it.done })
        }
        return arr
    }

    function _appendEmptyAfter(index) {
        itemsModel.insert(index + 1, { text: "", done: false })
        contentChanged()
    }

    Column {
        id: column
        width: parent.width
        spacing: 0

        Repeater {
            model: itemsModel

            delegate: Row {
                width: column.width
                spacing: Theme.paddingSmall

                MouseArea {
                    width: Theme.iconSizeSmall
                    height: Theme.iconSizeSmall
                    anchors.verticalCenter: parent.verticalCenter
                    enabled: block.richMode
                    onClicked: {
                        itemsModel.setProperty(index, "done", !model.done)
                        block.contentChanged()
                    }

                    Label {
                        anchors.centerIn: parent
                        text: model.done ? "☑" : "☐"
                        font.pixelSize: Theme.fontSizeMedium
                        color: model.done ? Theme.secondaryColor : Theme.primaryColor
                    }
                }

                TextField {
                    id: itemField
                    width: parent.width - Theme.iconSizeSmall - removeBtn.width
                           - 2 * Theme.paddingSmall
                    anchors.verticalCenter: parent.verticalCenter
                    text: model.text
                    placeholderText: qsTr("List item")
                    readOnly: !block.richMode
                    color: model.done ? Theme.secondaryColor : Theme.primaryColor
                    font.strikeout: model.done

                    onTextChanged: {
                        if (text !== model.text) {
                            itemsModel.setProperty(index, "text", text)
                            block.contentChanged()
                        }
                    }

                    EnterKey.iconSource: "image://theme/icon-m-enter-next"
                    EnterKey.onClicked: {
                        if (text.length === 0) {
                            // Empty item + Enter → close the list and let the
                            // editor open a text block after this checklist.
                            itemsModel.remove(index)
                            if (itemsModel.count === 0) {
                                itemsModel.append({ text: "", done: false })
                            }
                            block.requestEndList(block.blockIndex)
                        } else {
                            block._appendEmptyAfter(index)
                        }
                    }

                    Keys.onPressed: {
                        if ((event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete)
                            && text.length === 0 && itemsModel.count > 1) {
                            itemsModel.remove(index)
                            block.contentChanged()
                            event.accepted = true
                        }
                    }
                }

                IconButton {
                    id: removeBtn
                    icon.source: "image://theme/icon-s-clear-opaque-cross"
                    visible: block.richMode
                    width: visible ? Theme.iconSizeSmall : 0
                    anchors.verticalCenter: parent.verticalCenter
                    onClicked: {
                        if (itemsModel.count <= 1) {
                            block.requestRemoveBlock(block.blockIndex)
                        } else {
                            itemsModel.remove(index)
                            block.contentChanged()
                        }
                    }
                }
            }
        }

        IconButton {
            id: addBtn
            visible: block.richMode
            icon.source: "image://theme/icon-m-add"
            anchors.left: parent.left
            onClicked: {
                itemsModel.append({ text: "", done: false })
                block.contentChanged()
            }
        }
    }
}
