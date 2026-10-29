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
    required property var mission
    required property var voice
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

    // 語音倒數中按 Esc 取消；任何視窗都有效，只在倒數時啟用，不影響對話框的 Esc
    Shortcut {
        sequence: "Esc"
        context: Qt.ApplicationShortcut
        enabled: window.mission.isAutoStartPending
        onActivated: window.mission.cancelStart()
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
            mission: window.mission
        }

        CommandPanel {
            id: commandPanel

            x: Theme.spaceLg
            y: Theme.spaceLg
            z: 20
            isOpen: !window.isNarrow || window.isCommandPanelInitiallyOpen
            bridge: window.bridge
            settings: window.settings
            mission: window.mission
            voice: window.voice
        }

        FleetPanel {
            id: fleetPanel

            x: window.isNarrow ? Theme.spaceLg : mainArea.width - width - Theme.spaceLg
            y: window.isNarrow ? commandPanel.y + commandPanel.height + Theme.spaceMd : Theme.spaceLg
            z: 10
            // 保留右下角縮放按鈕與左下角資訊列空間
            maximumHeight: mainArea.height - y - 140
            bridge: window.bridge
            settings: window.settings
        }

        // 失敗與警告提示：放在飛行控制與機群面板之間的空白處，不遮住面板；
        // 空間不足（窄視窗）時改放在底部，避開左下資訊列與右下縮放按鈕
        Toasts {
            id: toasts

            readonly property real leftBound: commandPanel.x + commandPanel.width + Theme.spaceLg
            readonly property real rightBound: window.isNarrow ? mainArea.width - Theme.spaceLg
                                                               : fleetPanel.x - Theme.spaceLg
            readonly property bool hasTopGap: rightBound - leftBound >= 280

            x: hasTopGap ? leftBound + (rightBound - leftBound - width) / 2 : Theme.spaceLg
            y: hasTopGap ? Theme.spaceLg : mainArea.height - height - 40
            width: hasTopGap ? Math.min(460, rightBound - leftBound) : mainArea.width - 2 * Theme.spaceLg - 50
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
