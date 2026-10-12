/**
 * @file ConfirmDialog.qml
 * @brief L1 防呆：兩步驟確認對話框，顯示目前狀態供操作者確認（ADR-0006）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: root

    property string title: ""
    property string message: ""
    property string statusInfo: ""

    /// 確認執行
    signal accepted()

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: Math.min(560, parent ? parent.width - 32 : 560)
    padding: 26

    Overlay.modal: Rectangle {
        color: Qt.rgba(2 / 255, 8 / 255, 14 / 255, 0.78)
    }

    background: Rectangle {
        color: Theme.panel
        radius: 12
        border.color: Theme.borderStrong
    }

    contentItem: ColumnLayout {
        spacing: 14

        Label {
            text: root.title
            color: "#ffb3b3"
            font.pixelSize: Theme.fontTitle
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        Label {
            text: root.message
            color: Theme.textBody
            font.pixelSize: Theme.fontBase
            wrapMode: Text.WordWrap
            lineHeight: 1.4
            Layout.fillWidth: true
        }

        Rectangle {
            visible: root.statusInfo.length > 0
            color: Theme.inset
            radius: 8
            Layout.fillWidth: true
            implicitHeight: statusText.implicitHeight + 24

            Label {
                id: statusText

                anchors.fill: parent
                anchors.margins: 12
                text: qsTr("目前狀態：") + root.statusInfo
                color: "#bdcbd5"
                font.pixelSize: Theme.fontBase
                wrapMode: Text.WordWrap
            }
        }

        RowLayout {
            spacing: 12
            Layout.alignment: Qt.AlignRight
            Layout.topMargin: 6

            ActionButton {
                text: qsTr("取消")
                textAlignment: Text.AlignHCenter
                onClicked: root.close()
            }

            Button {
                id: confirmButton

                text: qsTr("確認執行")
                implicitHeight: 48
                font.pixelSize: Theme.fontBase
                leftPadding: 18
                rightPadding: 18
                contentItem: Text {
                    text: confirmButton.text
                    font: confirmButton.font
                    color: "white"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: 8
                    color: confirmButton.down ? "#5a1515" : "#7a2020"
                    border.width: confirmButton.visualFocus ? 3 : 1
                    border.color: confirmButton.visualFocus ? Theme.accent : "#ff6b6b"
                }
                onClicked: {
                    root.close()
                    root.accepted()
                }
            }
        }
    }
}
