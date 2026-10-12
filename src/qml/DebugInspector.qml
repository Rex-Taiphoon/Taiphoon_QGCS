/**
 * @file DebugInspector.qml
 * @brief 唯讀除錯工具視窗：topic 清單、Agent 最新資料、事件紀錄（F-11）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: root

    required property var diagnostics

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    width: Math.min(900, parent ? parent.width * 0.92 : 900)
    height: Math.min(640, parent ? parent.height * 0.85 : 640)
    padding: 18
    onOpened: commandField.forceActiveFocus()

    Overlay.modal: Rectangle {
        color: Qt.rgba(2 / 255, 8 / 255, 14 / 255, 0.78)
    }

    background: Rectangle {
        color: Theme.panel
        radius: 12
        border.color: Theme.border
    }

    contentItem: ColumnLayout {
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("除錯工具")
                color: Theme.textStrong
                font.pixelSize: Theme.fontTitle
                Layout.fillWidth: true
            }
            Label {
                text: qsTr("唯讀，不提供發布或呼叫")
                color: Theme.warning
                font.pixelSize: 15
            }
            ActionButton {
                iconName: "close"
                accentColor: Theme.textMuted
                implicitWidth: 42
                implicitHeight: 42
                padding: 0
                Accessible.name: qsTr("關閉除錯工具")
                onClicked: root.close()
            }
        }

        ScrollView {
            id: outputScroll

            Layout.fillWidth: true
            Layout.fillHeight: true
            background: Rectangle { color: "#05090d"; radius: 6; border.color: Theme.borderSoft }

            TextArea {
                id: outputArea

                text: root.diagnostics.output
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.NoWrap
                color: "#cfe3d0"
                font.family: "Consolas, Menlo, monospace"
                font.pixelSize: 15
                background: Item {}
                onTextChanged: cursorPosition = length
            }
        }

        RowLayout {
            spacing: 8
            Layout.fillWidth: true

            Label { text: ">"; color: Theme.online; font.pixelSize: Theme.fontBase }
            TextField {
                id: commandField

                placeholderText: "topic list"
                color: Theme.textBody
                font.family: "Consolas, Menlo, monospace"
                font.pixelSize: 16
                Layout.fillWidth: true
                background: Rectangle {
                    color: Theme.input
                    radius: 4
                    border.color: commandField.activeFocus ? Theme.accent : "#555555"
                }
                onAccepted: {
                    root.diagnostics.execute(text)
                    text = ""
                }
            }
            ActionButton {
                text: qsTr("執行")
                textAlignment: Text.AlignHCenter
                onClicked: {
                    root.diagnostics.execute(commandField.text)
                    commandField.text = ""
                }
            }
        }
    }
}
