/**
 * @file SettingsDialog.qml
 * @brief 進階設定：自動跟隨、命令逾時、網路設定、Agent 關閉、除錯工具與結束 GCS（網頁版 SettingsPanel）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: root

    required property var bridge
    required property var settings

    /// 開啟網路設定
    signal siteSetupRequested()
    /// 開啟除錯工具
    signal inspectorRequested()

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: Math.min(560, parent ? parent.width * 0.9 : 560)
    height: Math.min(implicitHeight, parent ? parent.height * 0.85 : 700)
    padding: 0

    Overlay.modal: Rectangle {
        color: Qt.rgba(2 / 255, 8 / 255, 14 / 255, 0.78)
    }

    background: Rectangle {
        color: Theme.panel
        radius: 12
        border.color: Theme.border
    }

    contentItem: ColumnLayout {
        spacing: 0

        // 標題
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 18
            Layout.leftMargin: 22
            Label {
                text: qsTr("進階設定")
                color: Theme.textStrong
                font.pixelSize: Theme.fontTitle
                Layout.fillWidth: true
            }
            ActionButton {
                iconName: "close"
                accentColor: Theme.textMuted
                implicitWidth: 42
                implicitHeight: 42
                padding: 0
                Accessible.name: qsTr("關閉設定")
                onClicked: root.close()
            }
        }
        Rectangle { color: Theme.borderSoft; implicitHeight: 1; Layout.fillWidth: true }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: body.implicitHeight + 40
            contentHeight: body.implicitHeight + 40
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            ColumnLayout {
                id: body

                x: 22
                y: 20
                width: parent.width - 44
                spacing: 18

                // 自動跟隨
                RowLayout {
                    spacing: 12
                    Layout.fillWidth: true
                    CheckBox {
                        checked: root.settings.autoFollow
                        onToggled: root.settings.autoFollow = checked
                        Accessible.name: qsTr("自動跟隨選中無人機")
                    }
                    ColumnLayout {
                        spacing: 2
                        Layout.fillWidth: true
                        Label { text: qsTr("自動跟隨選中無人機"); color: Theme.textBody; font.pixelSize: Theme.fontBase }
                        Label {
                            text: qsTr("選取單一無人機時，地圖自動置中於其位置")
                            color: Theme.textMuted
                            font.pixelSize: 15
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                }

                // 命令逾時
                RowLayout {
                    spacing: 12
                    Layout.fillWidth: true
                    ColumnLayout {
                        spacing: 2
                        Layout.fillWidth: true
                        Label { text: qsTr("命令逾時（毫秒）"); color: Theme.textBody; font.pixelSize: Theme.fontBase }
                        Label {
                            text: qsTr("發送命令後若未在此時間內收到回執，視為失敗")
                            color: Theme.textMuted
                            font.pixelSize: 15
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                    SpinBox {
                        from: 1000
                        to: 30000
                        stepSize: 500
                        editable: true
                        value: root.settings.cmdTimeoutMs
                        font.pixelSize: Theme.fontBase
                        onValueModified: root.settings.cmdTimeoutMs = value
                        Accessible.name: qsTr("命令逾時")
                    }
                }

                // 網路設定
                Section {
                    title: qsTr("網路設定")
                    Label {
                        text: root.bridge.isLinkConnected
                              ? qsTr("DDS 已連線：%1").arg(root.bridge.linkMessage)
                              : qsTr("DDS 未連線：%1").arg(root.bridge.linkMessage)
                        color: root.bridge.isLinkConnected ? Theme.textMuted : Theme.warning
                        font.pixelSize: 15
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    ActionButton {
                        text: qsTr("變更 Agent 位址與網卡")
                        accentColor: Theme.accent
                        Layout.fillWidth: true
                        onClicked: {
                            root.close()
                            root.siteSetupRequested()
                        }
                    }
                }

                // 進階操作：Agent 關閉
                Section {
                    title: qsTr("進階操作")
                    Label {
                        text: qsTr("此操作會要求指定 Agent 結束程序，不會先執行返航或降落；飛行中執行將失去 Agent 控制，請勿使用。")
                        color: "#ffb74d"
                        font.pixelSize: 15
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    ActionButton {
                        text: qsTr("關閉 %1").arg(root.bridge.canShutdownAgent ? root.bridge.selectedAgent : "Agent")
                        accentColor: "#ff8a80"
                        enabled: root.bridge.canShutdownAgent
                        Layout.fillWidth: true
                        onClicked: agentShutdownDialog.open()
                    }
                }

                // 除錯工具
                Section {
                    title: qsTr("除錯工具")
                    Label {
                        text: qsTr("唯讀：列出 DDS topic、檢視 Agent 資料與事件紀錄；不提供發布命令")
                        color: Theme.textMuted
                        font.pixelSize: 15
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    ActionButton {
                        text: qsTr("開啟除錯工具")
                        Layout.fillWidth: true
                        onClicked: {
                            root.close()
                            root.inspectorRequested()
                        }
                    }
                }
            }
        }

        Rectangle { color: Theme.borderSoft; implicitHeight: 1; Layout.fillWidth: true }

        // 底部：結束 GCS、恢復預設值、完成
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 16
            Layout.leftMargin: 22
            Layout.rightMargin: 22
            spacing: 12

            ActionButton {
                iconName: "power"
                accentColor: "#ff8a80"
                implicitWidth: 48
                tooltip: qsTr("結束 GCS")
                Accessible.name: qsTr("結束 GCS")
                onClicked: exitDialog.open()
            }
            Item { Layout.fillWidth: true }
            ActionButton {
                text: qsTr("恢復預設值")
                textAlignment: Text.AlignHCenter
                onClicked: root.settings.resetUiSettings()
            }
            ActionButton {
                text: qsTr("完成")
                textAlignment: Text.AlignHCenter
                accentColor: Theme.accentSoft
                onClicked: root.close()
            }
        }
    }

    ConfirmDialog {
        id: exitDialog

        title: qsTr("確認結束 GCS")
        message: qsTr("GCS 將結束並停止監控。無人機與 Agent 仍會繼續執行，GCS 不會發送任何命令。")
        onAccepted: Qt.quit()
    }

    ConfirmDialog {
        id: agentShutdownDialog

        title: qsTr("確認關閉 Taiphoon_Agent")
        message: qsTr("即將關閉 %1 的 Taiphoon_Agent。此操作無法由 GCS 自動復原，且不會觸發 RTL 或降落。")
                 .arg(root.bridge.selectedAgent || qsTr("指定"))
        statusInfo: (root.bridge.selectedSummary.modeLabel || qsTr("等待遙測資料")) + "；"
                    + (root.bridge.selectedSummary.px4Label || qsTr("PX4 狀態未確認"))
        onAccepted: root.bridge.requestAgentShutdown()
    }

    component Section: ColumnLayout {
        property string title: ""
        property bool isExpanded: false
        default property alias content: sectionBody.data

        spacing: 8
        Layout.fillWidth: true

        AbstractButton {
            id: sectionHeader

            Layout.fillWidth: true
            implicitHeight: 40
            onClicked: parent.isExpanded = !parent.isExpanded
            Accessible.name: parent.title
            contentItem: Label {
                text: (sectionHeader.parent.isExpanded ? "▾ " : "▸ ") + sectionHeader.parent.title
                color: Theme.textBody
                font.pixelSize: Theme.fontBase
                font.bold: true
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: 8
                color: Theme.panelDeep
                border.color: sectionHeader.visualFocus ? Theme.accent : Theme.borderSoft
            }
        }
        ColumnLayout {
            id: sectionBody

            visible: parent.isExpanded
            spacing: 8
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
        }
    }
}
