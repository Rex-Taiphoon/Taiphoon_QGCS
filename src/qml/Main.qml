/**
 * @file Main.qml
 * @brief 主視窗：60 px 狀態列 + 全幅地圖，飛行控制與機群視窗懸浮其上（ADR-0009）
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
    /// 啟動時展開飛行控制（開發驗證畫面使用）
    property bool isCommandPanelInitiallyOpen: false
    /// 窄視窗時機群視窗移至飛行控制入口下方（網頁版 600 px 斷點）
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
            isOpen: window.isCommandPanelInitiallyOpen
            bridge: window.bridge
            settings: window.settings
        }

        FleetPanel {
            x: window.isNarrow ? 16 : mainArea.width - width - 16
            y: window.isNarrow ? 84 : 16
            z: 10
            maximumHeight: mainArea.height - (window.isNarrow ? 194 : 126)
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
