/**
 * @file Main.qml
 * @brief 主視窗：48 px 狀態列 + 全幅地圖，飛行控制、機群與影像面板懸浮其上（ADR-0009、ADR-0019）
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
    required property var video
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

    // 影像獨立視窗不隸屬主視窗（可被遮住、單獨最小化），主視窗關閉時須明確結束程式
    onClosing: Qt.quit()

    Component.onCompleted: {
        if (window.isFirstRunAllowed && !window.settings.isSiteConfigured) {
            siteSetup.isFirstRun = true
            siteSetup.open()
        }
    }

    // 語音倒數中按 Esc 取消；任何視窗（含獨立影像視窗）都有效，只在倒數時啟用，不影響對話框的 Esc
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
            // 保留右下角縮放按鈕、影像面板與左下角資訊列空間
            maximumHeight: mainArea.height - y - Math.max(140, videoPanel.height + 2 * Theme.spaceLg)
            bridge: window.bridge
            settings: window.settings
            isVideoAvailable: window.video.isAvailable
            onVideoSourceRequested: (agent) => videoSourceDialog.openFor(agent)
        }

        // 影像縮圖面板：右下角、地圖縮放按鈕左側。只在有 Qt Multimedia 的平台載入（ADR-0019）
        Loader {
            id: videoPanel

            /// 地圖右下角縮放按鈕的寬度（34 px 按鈕加外框）
            readonly property int mapControlsWidth: 36
            readonly property real leftBound: window.isNarrow ? Theme.spaceLg
                                                              : commandPanel.x + commandPanel.width + Theme.spaceLg

            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.rightMargin: Theme.spaceLg + mapControlsWidth + Theme.spaceSm
            anchors.bottomMargin: Theme.spaceLg
            z: 15
            active: window.video.isAvailable
            source: active ? "VideoPanel.qml" : ""
            onLoaded: {
                item.bridge = window.bridge
                item.video = window.video
                item.maximumWidth = Qt.binding(() => mainArea.width - videoPanel.anchors.rightMargin
                                                     - videoPanel.leftBound)
                item.maximumHeight = Qt.binding(() => Math.max(videoPanel.item.headerHeight,
                                                               mainArea.height * 0.5))
            }
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

    VideoSourceDialog {
        id: videoSourceDialog

        bridge: window.bridge
        video: window.video
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
