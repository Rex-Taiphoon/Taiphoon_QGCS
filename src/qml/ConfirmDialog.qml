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
    padding: Theme.spaceXl

    Overlay.modal: Rectangle {
        color: Qt.rgba(2 / 255, 8 / 255, 14 / 255, 0.78)
    }

    background: Rectangle {
        color: Theme.panel
        radius: Theme.radius
        border.color: Theme.caution
    }

    contentItem: ColumnLayout {
        spacing: 14

        Label {
            text: root.title
            color: Theme.caution
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
            radius: Theme.radius
            border.color: Theme.borderSoft
            Layout.fillWidth: true
            implicitHeight: statusText.implicitHeight + 24

            Label {
                id: statusText

                anchors.fill: parent
                anchors.margins: 12
                text: qsTr("目前狀態：") + root.statusInfo
                color: Theme.textBody
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
                implicitHeight: Theme.controlHeight
                font.pixelSize: Theme.fontBase
                leftPadding: 18
                rightPadding: 18
                contentItem: Text {
                    text: confirmButton.text
                    font: confirmButton.font
                    color: Theme.caution
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    radius: Theme.radius
                    color: confirmButton.down ? Theme.controlActive : Theme.cautionInset
                    border.width: confirmButton.visualFocus ? 2 : 1
                    border.color: confirmButton.visualFocus ? Theme.accent : Theme.caution
                }
                onClicked: {
                    root.close()
                    root.accepted()
                }
            }
        }
    }
}
