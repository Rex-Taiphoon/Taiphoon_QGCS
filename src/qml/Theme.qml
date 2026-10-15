/**
 * @file Theme.qml
 * @brief 共用色彩、字級與間距刻度（航電座艙風格：黑底、直角、顏色只表達狀態）
 * @author Taiphoon GCS Team
 * @date 2026-10-15
 *
 * 色彩語意比照民航玻璃座艙：綠＝正常、琥珀＝注意、紅＝警告、青＝可操作、洋紅＝目前航跡。
 * 命令按鈕不再依命令種類上色，只依風險分三級：一般（白）、需確認（琥珀）、危險（紅）。
 */
pragma Singleton

import QtQuick

QtObject {
    // 底色與面板
    readonly property color background: "#0a0c0d"
    readonly property color bar: "#0a0c0d"
    readonly property color panel: Qt.rgba(10 / 255, 12 / 255, 13 / 255, 0.94)
    readonly property color panelDeep: "#0e1112"
    readonly property color inset: "#121618"
    readonly property color control: "#121618"
    readonly property color controlHover: "#182023"
    readonly property color controlActive: "#13292e"
    readonly property color input: "#0e1112"
    /// 確認列底色（琥珀注意）
    readonly property color cautionInset: "#17120a"

    // 線條
    readonly property color border: "#2b3236"
    readonly property color borderSoft: "#1d2427"
    readonly property color borderStrong: "#3d474c"

    // 文字
    readonly property color text: "#e4eaea"
    readonly property color textStrong: "#f2f5f5"
    readonly property color textBody: "#d9e0e0"
    readonly property color textMuted: "#7d8a8f"
    readonly property color textDim: "#4e5a5e"
    readonly property color textValue: "#e4eaea"

    // 語意色（狀態）
    /// 正常、已啟用、線上
    readonly property color normal: "#5fd38d"
    /// 注意：需確認的命令、等待飛控
    readonly property color caution: "#ffb000"
    /// 警告：Arm、Disarm、連線中斷、錯誤
    readonly property color danger: "#ff5a52"
    /// 可操作：選取、焦點、輸入欄位
    readonly property color accent: "#56d4ea"
    /// 目前航跡
    readonly property color route: "#e36ef2"
    /// 線上（沿用名稱，等同 normal）
    readonly property color online: normal

    // 命令風險等級色
    readonly property color commandNormal: textStrong
    readonly property color commandConfirm: caution
    readonly property color commandDanger: danger

    // 地圖標記
    readonly property color markerSelected: accent
    readonly property color markerNormal: textStrong
    readonly property color trajectorySelected: route
    readonly property color trajectoryOther: Qt.rgba(227 / 255, 110 / 255, 242 / 255, 0.45)

    // 姿態儀（PFD 天藍與地棕）
    readonly property color sky: "#1d5f8f"
    readonly property color ground: "#6b4524"

    // 字型：B612 為 Airbus 駕駛艙顯示器字型（main.cpp 註冊，中文由系統字型備援）；
    // 等寬字用於所有數值與代號，跳動時不左右晃動
    readonly property string fontFamily: "B612"
    readonly property string fontMono: "B612 Mono"

    // 字級刻度
    /// 欄位標籤（大寫、加字距）
    readonly property int fontLabel: 11
    readonly property int fontSmall: 13
    readonly property int fontBase: 15
    /// 遙測數值
    readonly property int fontValue: 16
    readonly property int fontTitle: 20
    /// 標籤字距（像素）
    readonly property real labelSpacing: 1.4

    // 間距刻度（4 的倍數）
    readonly property int spaceXs: 4
    readonly property int spaceSm: 8
    readonly property int spaceMd: 12
    readonly property int spaceLg: 16
    readonly property int spaceXl: 24

    // 圓角：面板與按鈕一律直角，輸入欄位保留極小圓角
    readonly property int radius: 0
    readonly property int radiusInput: 2

    // 控制項高度
    readonly property int controlHeight: 40
    readonly property int rowHeight: 36

    readonly property int disabledOpacityPercent: 35
    readonly property real disabledOpacity: 0.35
}
