/**
 * @file VideoPanel.qml
 * @brief 影像縮圖面板：右下角懸浮，每台線上無人機一格串流縮圖，可最小化；每格可彈出獨立視窗（ADR-0019）
 * @author Taiphoon GCS Team
 * @date 2026-10-29
 *
 * 機隊清單取自 UiBridge.fleetModel（DDS 發現），不寫死數量；URL 由 VideoController 解析。
 * 每台一個 VideoPlayer，縮圖與獨立視窗共用同一路連線。面板最小化後，沒有開啟獨立視窗的串流會停止。
 * 只在有 Qt Multimedia 的平台建置，由 Main.qml 以 Loader 載入。
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property var bridge: null
    property var video: null
    /// 由父項目設定的可用寬高，避開飛行控制面板與地圖按鈕
    property real maximumWidth: 800
    property real maximumHeight: 400

    readonly property bool isOpen: video !== null && video.isPanelOpen
    readonly property int headerHeight: 40
    /// 縮圖尺寸（16:9）
    readonly property int tileWidth: 184
    readonly property int tileHeight: Math.round(tileWidth * 9 / 16)
    readonly property int tileCount: bridge !== null ? bridge.onlineCount : 0
    readonly property int columns: Math.max(1, Math.min(Math.max(tileCount, 1),
                                                        Math.floor((maximumWidth - 2 * Theme.spaceSm + Theme.spaceSm)
                                                                   / (tileWidth + Theme.spaceSm))))
    readonly property real openWidth: Math.max(header.implicitWidth + 2 * Theme.spaceLg,
                                               columns * (tileWidth + Theme.spaceSm) + Theme.spaceSm)

    width: isOpen ? Math.min(openWidth, maximumWidth) : header.implicitWidth + 2 * Theme.spaceLg
    height: isOpen ? Math.min(headerHeight + 1 + body.implicitHeight, maximumHeight) : headerHeight
    color: Theme.panel
    radius: Theme.radius
    border.color: Theme.border
    clip: true

    RowLayout {
        id: header

        x: Theme.spaceLg
        width: root.isOpen ? root.width - Theme.spaceLg - Theme.spaceXs : implicitWidth
        height: root.headerHeight
        spacing: Theme.spaceMd

        Label {
            text: "VIDEO"
            color: Theme.textBody
            font.pixelSize: Theme.fontLabel + 1
            font.letterSpacing: Theme.labelSpacing
        }
        Label {
            visible: root.video !== null && !root.video.isConfigured
            text: qsTr("未設定")
            color: Theme.textMuted
            font.pixelSize: Theme.fontSmall
        }
        Item {
            Layout.fillWidth: root.isOpen
        }
        ActionButton {
            iconName: root.isOpen ? "minus" : "chevronUp"
            accentColor: Theme.textMuted
            implicitWidth: 32
            implicitHeight: 32
            padding: 0
            background: Item {}
            tooltip: root.isOpen ? qsTr("最小化影像面板（已開啟的獨立視窗繼續播放）") : qsTr("展開影像面板")
            Accessible.name: tooltip
            onClicked: root.video.isPanelOpen = !root.isOpen
        }
    }

    Rectangle {
        visible: root.isOpen
        y: root.headerHeight
        width: parent.width
        height: 1
        color: Theme.border
    }

    // 面板最小化時只隱藏，不銷毀：各格的播放器與獨立視窗需要繼續存在
    Flickable {
        id: content

        visible: root.isOpen
        anchors.fill: parent
        anchors.topMargin: root.headerHeight + 1
        contentHeight: body.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        Column {
            id: body

            width: content.width
            topPadding: Theme.spaceSm
            bottomPadding: Theme.spaceSm
            spacing: Theme.spaceSm

            Label {
                visible: root.video !== null && !root.video.isConfigured
                width: parent.width
                leftPadding: Theme.spaceMd
                rightPadding: Theme.spaceMd
                text: qsTr("尚未設定影像來源：在機群表的「全部」或單機上按右鍵設定串流網址")
                color: Theme.textMuted
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }
            Label {
                visible: root.tileCount === 0
                leftPadding: Theme.spaceMd
                text: qsTr("等待 Agent 上線...")
                color: Theme.textMuted
                font.pixelSize: Theme.fontSmall
            }

            Grid {
                x: Theme.spaceSm
                columns: root.columns
                spacing: Theme.spaceSm

                Repeater {
                    model: root.bridge !== null ? root.bridge.fleetModel : null
                    delegate: VideoTile {
                        width: root.tileWidth
                        height: root.tileHeight
                        bridge: root.bridge
                        video: root.video
                        isPanelOpen: root.isOpen
                    }
                }
            }
        }
    }
}
