/**
 * @file Theme.qml
 * @brief 共用色彩與字級（沿用網頁版配色）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
pragma Singleton

import QtQuick

QtObject {
    // 底色與面板
    readonly property color background: "#0b1118"
    readonly property color bar: "#0d161f"
    readonly property color panel: "#111b25"
    readonly property color panelDeep: "#0d161f"
    readonly property color inset: "#0b121a"
    readonly property color control: "#182532"
    readonly property color controlActive: "#193752"
    readonly property color input: "#222222"

    // 邊框
    readonly property color border: "#4b6072"
    readonly property color borderSoft: "#334454"
    readonly property color borderStrong: "#5d7182"

    // 文字
    readonly property color text: "#eef4f8"
    readonly property color textStrong: "#f4f8fb"
    readonly property color textBody: "#e7eef3"
    readonly property color textMuted: "#a8bac8"
    readonly property color textDim: "#888888"
    readonly property color textValue: "#c6d3dc"

    // 強調色
    readonly property color accent: "#42a5f5"
    readonly property color accentSoft: "#90caf9"
    readonly property color warning: "#ffca55"
    readonly property color online: "#58d68d"
    readonly property color danger: "#ff4444"

    // 命令按鈕色（網頁版 CommandPanel）
    readonly property color takeoff: "#44aaff"
    readonly property color offboard: "#22ff88"
    readonly property color rtl: "#ffaa00"
    readonly property color land: "#8888ff"
    readonly property color disarm: "#ff8888"
    readonly property color formation: "#00ff88"
    readonly property color cancel: "#ff6666"
    readonly property color yaw: "#c084fc"

    // 地圖標記（網頁版 MapView）
    readonly property color markerSelected: "#00ff88"
    readonly property color markerNormal: "#4488ff"
    readonly property color trajectoryOther: "#ffb020"

    // 字級（網頁版基準 18 px）
    readonly property int fontBase: 18
    readonly property int fontSmall: 16
    readonly property int fontTitle: 26
    readonly property int disabledOpacityPercent: 35
    readonly property real disabledOpacity: 0.35
}
