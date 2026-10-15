/**
 * @file Toasts.qml
 * @brief 可堆疊提示訊息（命令失敗與系統警告；成功訊息只寫日誌），6 秒後自動消失（網頁版 ToastContainer）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Column {
    id: root

    required property var bridge

    width: 460
    spacing: Theme.spaceSm

    Repeater {
        model: root.bridge.toastModel
        delegate: Rectangle {
            id: toast

            required property int toastId
            required property string kind
            required property string title
            required property string message

            readonly property color accent: kind === "success" ? Theme.normal : (kind === "error" ? Theme.danger : Theme.caution)

            width: root.width
            implicitHeight: toastRow.implicitHeight + 2 * Theme.spaceMd
            radius: Theme.radius
            color: Theme.panel
            border.color: accent
            Accessible.role: Accessible.AlertMessage
            Accessible.name: title + "：" + message

            RowLayout {
                id: toastRow

                anchors.fill: parent
                anchors.margins: Theme.spaceMd
                anchors.leftMargin: Theme.spaceLg
                spacing: Theme.spaceMd

                Icon {
                    name: toast.kind === "success" ? "check" : (toast.kind === "error" ? "close" : "alert")
                    color: toast.accent
                    implicitWidth: 20
                    implicitHeight: 20
                    Layout.alignment: Qt.AlignTop
                }
                ColumnLayout {
                    spacing: 4
                    Layout.fillWidth: true
                    Label {
                        text: toast.title
                        color: toast.accent
                        font.pixelSize: Theme.fontBase
                        font.bold: true
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Label {
                        text: toast.message
                        color: Theme.textBody
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.WrapAnywhere
                        Layout.fillWidth: true
                    }
                }
                ToolButton {
                    Accessible.name: qsTr("關閉通知")
                    Layout.alignment: Qt.AlignTop
                    onClicked: root.bridge.toastModel.remove(toast.toastId)
                    contentItem: Icon { name: "close"; color: Theme.textMuted; implicitWidth: 18; implicitHeight: 18 }
                    background: Item {}
                }
            }
        }
    }
}
