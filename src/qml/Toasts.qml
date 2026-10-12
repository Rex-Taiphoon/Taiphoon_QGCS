/**
 * @file Toasts.qml
 * @brief 可堆疊提示訊息（命令結果與系統警告），6 秒後自動消失（網頁版 ToastContainer）
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
    spacing: 10

    Repeater {
        model: root.bridge.toastModel
        delegate: Rectangle {
            id: toast

            required property int toastId
            required property string kind
            required property string title
            required property string message

            readonly property color accent: kind === "success" ? "#00ff00" : (kind === "error" ? "#ff4444" : "#ffaa00")

            width: root.width
            implicitHeight: toastRow.implicitHeight + 28
            radius: 10
            color: kind === "success" ? "#0a2a0a" : (kind === "error" ? "#2a0a0a" : "#2a2a0a")
            border.color: accent
            Accessible.role: Accessible.AlertMessage
            Accessible.name: title + "：" + message

            RowLayout {
                id: toastRow

                anchors.fill: parent
                anchors.margins: 14
                anchors.leftMargin: 16
                spacing: 12

                Label {
                    text: toast.kind === "success" ? "✓" : (toast.kind === "error" ? "✕" : "⚠")
                    color: toast.accent
                    font.pixelSize: 22
                    font.bold: true
                    Layout.alignment: Qt.AlignTop
                }
                ColumnLayout {
                    spacing: 4
                    Layout.fillWidth: true
                    Label {
                        text: toast.title
                        color: "#eeeeee"
                        font.pixelSize: 20
                        font.bold: true
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Label {
                        text: toast.message
                        color: "#aaaaaa"
                        font.pixelSize: Theme.fontBase
                        wrapMode: Text.WrapAnywhere
                        Layout.fillWidth: true
                    }
                }
                ToolButton {
                    Accessible.name: qsTr("關閉通知")
                    Layout.alignment: Qt.AlignTop
                    onClicked: root.bridge.toastModel.remove(toast.toastId)
                    contentItem: Icon { name: "close"; color: "#888888"; implicitWidth: 20; implicitHeight: 20 }
                    background: Item {}
                }
            }
        }
    }
}
