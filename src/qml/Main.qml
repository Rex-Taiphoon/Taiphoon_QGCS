/**
 * @file Main.qml
 * @brief 主視窗：48 px 狀態列 + 全幅地圖，飛行控制與機群視窗懸浮其上（ADR-0009）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: window

    required property var bridge
    required property var settings
    required property var network
    required property var diagnostics
    /// 是否允許顯示首次設定精靈（命令列指定網路時為 false）
    property bool isFirstRunAllowed: true
    /// 啟動時展開飛行控制；窄視窗預設收合，開發驗證畫面可強制展開
    property bool isCommandPanelInitiallyOpen: false
    /// 窄視窗時機群視窗移至飛行控制面板下方（網頁版 600 px 斷點）
    readonly property bool isNarrow: width <= 600

    width: 1280
    height: 800
    minimumWidth: 480
    minimumHeight: 480
    visible: true
    title: qsTr("Taiphoon_QGCS")
    color: Theme.background
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontBase
    // Basic 樣式的提示框取自調色盤：改為深色底，與座艙配色一致
    palette.toolTipBase: Theme.panelDeep
    palette.toolTipText: Theme.text
    palette.dark: Theme.borderStrong

    Component.onCompleted: {
        if (window.isFirstRunAllowed && !window.settings.isSiteConfigured) {
            siteSetup.isFirstRun = true
            siteSetup.open()
        }
    }

    header: StatusBar {
        bridge: window.bridge
        onSettingsRequested: settingsDialog.open()
    }

    Item {
        id: mainArea

        anchors.fill: parent

        MapView {
            anchors.fill: parent
            bridge: window.bridge
            settings: window.settings
        }

        CommandPanel {
            id: commandPanel

            x: Theme.spaceLg
            y: Theme.spaceLg
            z: 20
            isOpen: !window.isNarrow || window.isCommandPanelInitiallyOpen
            bridge: window.bridge
            settings: window.settings
        }

        FleetPanel {
            x: window.isNarrow ? Theme.spaceLg : mainArea.width - width - Theme.spaceLg
            y: window.isNarrow ? commandPanel.y + commandPanel.height + Theme.spaceMd : Theme.spaceLg
            z: 10
            // 保留右下角縮放按鈕與左下角資訊列空間
            maximumHeight: mainArea.height - y - 140
            bridge: window.bridge
            settings: window.settings
        }

        Toasts {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.topMargin: 14
            anchors.rightMargin: 18
            width: Math.min(460, mainArea.width - 36)
            z: 100
            bridge: window.bridge
        }
    }

    SettingsDialog {
        id: settingsDialog

        bridge: window.bridge
        settings: window.settings
        onSiteSetupRequested: {
            siteSetup.isFirstRun = false
            siteSetup.open()
        }
        onInspectorRequested: inspector.open()
    }

    SiteSetupDialog {
        id: siteSetup

        settings: window.settings
        network: window.network
    }

    DebugInspector {
        id: inspector

        diagnostics: window.diagnostics
    }
}
