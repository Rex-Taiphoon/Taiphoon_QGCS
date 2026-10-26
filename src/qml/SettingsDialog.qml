/**
 * @file SettingsDialog.qml
 * @brief 進階設定：自動跟隨、網路設定、地圖顯示位置、Agent 關閉、除錯工具與結束 GCS（網頁版 SettingsPanel）
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
    /// 地圖顯示位置套用結果或錯誤訊息
    property string originMessage: ""
    property bool isOriginError: false

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
    onAboutToShow: loadDisplayOrigin()

    Overlay.modal: Rectangle {
        color: Qt.rgba(2 / 255, 8 / 255, 14 / 255, 0.78)
    }

    background: Rectangle {
        color: Theme.panel
        radius: Theme.radius
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
                            font.pixelSize: Theme.fontSmall
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                }

                // 網路設定
                Section {
                    title: qsTr("網路設定")
                    Label {
                        text: root.bridge.isLinkConnected
                              ? qsTr("DDS 已連線：%1").arg(root.bridge.linkMessage)
                              : qsTr("DDS 未連線：%1").arg(root.bridge.linkMessage)
                        color: root.bridge.isLinkConnected ? Theme.textMuted : Theme.caution
                        font.pixelSize: Theme.fontSmall
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

                // 地圖顯示位置：只平移畫面座標，命令、飛控與除錯工具仍使用 Agent 回報的原始位置
                Section {
                    title: qsTr("地圖顯示位置")
                    RowLayout {
                        spacing: 12
                        Layout.fillWidth: true
                        CheckBox {
                            id: originBox

                            Accessible.name: qsTr("以實際場地位置顯示")
                        }
                        ColumnLayout {
                            spacing: 2
                            Layout.fillWidth: true
                            Label { text: qsTr("以實際場地位置顯示"); color: Theme.textBody; font.pixelSize: Theme.fontBase }
                            Label {
                                text: qsTr("UWB 送出的 GPS 原點無法修改時使用。只改變地圖、軌跡與機群的顯示位置，飛控與命令不受影響。")
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontSmall
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                        }
                    }
                    Label {
                        text: qsTr("原始座標（緯度, 經度）")
                        color: Theme.textBody
                        font.pixelSize: Theme.fontSmall
                    }
                    RowLayout {
                        spacing: 8
                        Layout.fillWidth: true
                        TextField {
                            id: sourceField

                            placeholderText: qsTr("例如 22.5430960, 113.9364040")
                            color: Theme.textBody
                            font.pixelSize: Theme.fontBase
                            Layout.fillWidth: true
                            Accessible.name: qsTr("原始座標")
                            background: Rectangle {
                                color: Theme.input
                                radius: Theme.radiusInput
                                border.color: sourceField.activeFocus ? Theme.accent : Theme.borderStrong
                            }
                        }
                        ActionButton {
                            text: qsTr("帶入選取機位置")
                            tooltip: qsTr("以選取單機目前回報的位置作為原始座標")
                            enabled: (root.bridge.selectedSummary.reportedPositionText || "").length > 0
                            onClicked: sourceField.text = root.bridge.selectedSummary.reportedPositionText
                        }
                    }
                    Label {
                        text: qsTr("參考點在 Agent 回報資料中的位置，例如 UWB 原點；也可把飛機放在已知地點後按「帶入選取機位置」。")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Label {
                        text: qsTr("地圖座標（緯度, 經度）")
                        color: Theme.textBody
                        font.pixelSize: Theme.fontSmall
                    }
                    TextField {
                        id: displayField

                        placeholderText: qsTr("例如 25.0339640, 121.5644680")
                        color: Theme.textBody
                        font.pixelSize: Theme.fontBase
                        Layout.fillWidth: true
                        Accessible.name: qsTr("地圖座標")
                        background: Rectangle {
                            color: Theme.input
                            radius: Theme.radiusInput
                            border.color: displayField.activeFocus ? Theme.accent : Theme.borderStrong
                        }
                    }
                    Label {
                        text: qsTr("同一個參考點實際所在的位置，可直接貼上地圖服務複製的座標。")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Label {
                        visible: root.originMessage.length > 0
                        text: root.originMessage
                        color: root.isOriginError ? Theme.danger : Theme.textMuted
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    ActionButton {
                        text: qsTr("套用顯示位置")
                        accentColor: Theme.accent
                        Layout.fillWidth: true
                        onClicked: {
                            const error = root.settings.saveDisplayOrigin(originBox.checked, sourceField.text,
                                                                          displayField.text)
                            root.isOriginError = error.length > 0
                            if (root.isOriginError) {
                                root.originMessage = error
                            } else {
                                root.originMessage = originBox.checked ? qsTr("已套用：地圖以實際場地位置顯示")
                                                                       : qsTr("已套用：地圖顯示原始位置")
                            }
                        }
                    }
                }

                // Agent 關閉：保留警告文字，確認對話框再次提示
                ColumnLayout {
                    spacing: 8
                    Layout.fillWidth: true
                    Label {
                        text: qsTr("此操作會要求指定 Agent 結束程序，不會先執行返航或降落；飛行中執行將失去 Agent 控制，請勿使用。")
                        color: Theme.caution
                        font.pixelSize: Theme.fontSmall
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    ActionButton {
                        text: qsTr("關閉 %1").arg(root.bridge.canShutdownAgent ? root.bridge.selectedAgent : "Agent")
                        accentColor: Theme.danger
                        enabled: root.bridge.canShutdownAgent
                        Layout.fillWidth: true
                        onClicked: agentShutdownDialog.open()
                    }
                }

                // 除錯工具
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
                accentColor: Theme.danger
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
                accentColor: Theme.accent
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
                radius: Theme.radius
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

    /** @brief 以保存的設定填入地圖顯示位置欄位，未套用的修改在重新開啟時捨棄 */
    function loadDisplayOrigin() {
        originBox.checked = settings.isDisplayOriginEnabled
        sourceField.text = settings.sourceOriginText
        displayField.text = settings.displayOriginText
        originMessage = ""
        isOriginError = false
    }
}
