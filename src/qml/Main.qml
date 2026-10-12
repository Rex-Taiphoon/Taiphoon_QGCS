/**
 * @file Main.qml
 * @brief 主視窗（M0 骨架：保留 ADR-0009 的頂列與全幅主畫面配置）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: window

    /// ADR-0009：頂部狀態列固定高度（像素）
    readonly property int statusBarHeight: 60

    width: 1280
    height: 800
    minimumWidth: 800
    minimumHeight: 600
    visible: true
    title: qsTr("Taiphoon_QGCS")
    color: "#1b1f24"

    header: Rectangle {
        id: statusBar

        height: window.statusBarHeight
        color: "#11151a"

        Label {
            anchors.left: parent.left
            anchors.leftMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Taiphoon_QGCS")
            color: "#e6e6e6"
            font.pixelSize: 20
            font.bold: true
        }

        Label {
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            text: Qt.application.version
            color: "#8a939e"
        }
    }

    Label {
        anchors.centerIn: parent
        text: qsTr("開發中：地圖、飛行控制與機群視窗將於 M4、M5 加入")
        color: "#8a939e"
        font.pixelSize: 16
    }
}
