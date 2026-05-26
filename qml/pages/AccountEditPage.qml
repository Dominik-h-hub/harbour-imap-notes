import QtQuick 2.0
import Sailfish.Silica 1.0

Dialog {
    id: page
    allowedOrientations: Orientation.All

    property qint64 accountId: -1
    readonly property bool isNew: accountId < 0

    canAccept: displayNameField.text.length > 0
               && hostField.text.length > 0
               && userField.text.length > 0

    Component.onCompleted: {
        if (!isNew) {
            var a = Accounts.account(accountId)
            displayNameField.text = a.displayName || ""
            hostField.text = a.imapHost || ""
            portField.text = (a.imapPort || 993).toString()
            securityCombo.currentIndex = a.security === "starttls" ? 1 : 0
            userField.text = a.username || ""
            notesRootField.text = a.notesRoot || "Notes"
        }
    }

    onAccepted: {
        var data = {
            "displayName": displayNameField.text,
            "imapHost": hostField.text,
            "imapPort": parseInt(portField.text) || 993,
            "security": securityCombo.currentIndex === 1 ? "starttls" : "ssl",
            "username": userField.text,
            "notesRoot": notesRootField.text || "Notes"
        }

        var id = isNew ? Accounts.createAccount(data) : accountId
        if (id >= 0) {
            Accounts.updateAccount(id, data)
            if (passwordField.text.length > 0) {
                Accounts.setPassword(id, passwordField.text)
            }
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.implicitHeight

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingMedium

            DialogHeader {
                acceptText: page.isNew ? qsTr("Add") : qsTr("Save")
            }

            TextField {
                id: displayNameField
                width: parent.width
                label: qsTr("Display name")
                placeholderText: qsTr("e.g. Personal Mail")
            }

            TextField {
                id: hostField
                width: parent.width
                label: qsTr("IMAP server")
                placeholderText: qsTr("imap.example.org")
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
            }

            TextField {
                id: portField
                width: parent.width
                label: qsTr("Port")
                text: "993"
                inputMethodHints: Qt.ImhDigitsOnly
            }

            ComboBox {
                id: securityCombo
                width: parent.width
                label: qsTr("Connection security")
                menu: ContextMenu {
                    MenuItem { text: qsTr("SSL / TLS (implicit)") }
                    MenuItem { text: qsTr("STARTTLS") }
                }
            }

            TextField {
                id: userField
                width: parent.width
                label: qsTr("Username")
                inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoAutoUppercase
            }

            PasswordField {
                id: passwordField
                width: parent.width
                label: page.isNew ? qsTr("Password")
                                  : qsTr("Password (leave empty to keep)")
            }

            TextField {
                id: notesRootField
                width: parent.width
                label: qsTr("Notes folder on server")
                text: "Notes"
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: testInProgress ? qsTr("Testing…") : qsTr("Test connection")
                enabled: !testInProgress
                         && hostField.text.length > 0
                         && userField.text.length > 0
                         && passwordField.text.length > 0

                property bool testInProgress: false

                onClicked: {
                    testInProgress = true
                    Sync.testAccount({
                        "displayName": displayNameField.text,
                        "imapHost": hostField.text,
                        "imapPort": parseInt(portField.text) || 993,
                        "security": securityCombo.currentIndex === 1 ? "starttls" : "ssl",
                        "username": userField.text,
                        "notesRoot": notesRootField.text || "Notes"
                    }, passwordField.text)
                }

                Connections {
                    target: Sync
                    onAccountTestResult: {
                        parent.testInProgress = false
                        testResult.text = message
                        testResult.color = success ? Theme.highlightColor : Theme.errorColor
                    }
                }
            }

            Label {
                id: testResult
                anchors.horizontalCenter: parent.horizontalCenter
                font.pixelSize: Theme.fontSizeExtraSmall
                wrapMode: Text.Wrap
                width: parent.width - 2 * Theme.horizontalPageMargin
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }
}
