/**
 * @file StatusBar.qml
 * @brief 頂部狀態列：品牌、選取目標的遙測儀表格（含 PX4 飛行模式）、DDS 連線、UTC+8 時鐘與設定入口（網頁版 StatusBar）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 每個欄位為「標籤在上、數值在下」的儀表格，以細分隔線區隔；標籤為英文大寫縮寫（座艙慣例），
 * 數值與狀態維持中文並一律使用等寬字。
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property var bridge

    /// 開啟進階設定
    signal settingsRequested()

    readonly property var summary: bridge.selectedSummary
    readonly property bool hasLiveTelemetry: bridge.isSingleSelected && summary.hasTelemetry === true
                                             && summary.isWaitingForFlightController !== true

    /**
     * @brief 數值後接暗色單位（StyledText）
     * @param value 數值文字，空值顯示「—」
     * @param unit 單位
     * @return StyledText 字串
     */
    function withUnit(value, unit) {
        return (value || "—") + "<font color=\"" + Theme.textDim + "\">" + unit + "</font>"
    }

    implicitHeight: 48
    color: Theme.bar

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.border
    }

    RowLayout {
        anchors.fill: parent
        anchors.bottomMargin: 1
        spacing: 0

        // 品牌：橫式 Logo（白色去背，已含文字）
        Image {
            source: "qrc:/qt/qml/TaiphoonQgcs/resources/taiphoon-wordmark.png"
            fillMode: Image.PreserveAspectFit
            mipmap: true
            Layout.preferredHeight: 30
            Layout.preferredWidth: 30 * sourceSize.width / Math.max(1, sourceSize.height)
            Layout.leftMargin: Theme.spaceLg
            Layout.rightMargin: Theme.spaceLg
            Layout.alignment: Qt.AlignVCenter
            Accessible.name: "Taiphoon"
        }
        Separator {}

        // 摘要可水平捲動，連線狀態與設定入口保持可見
        Flickable {
            id: summaryArea

            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: summaryRow.implicitWidth
            contentHeight: height
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            ScrollBar.horizontal: ScrollBar { height: 3 }

            Row {
                id: summaryRow

                height: summaryArea.height

                // 全體
                Cell {
                    visible: root.bridge.isAllSelected
                    label: "TARGET"
                    value: qsTr("全部機群")
                    valueColor: Theme.accent
                }
                Cell {
                    visible: root.bridge.isAllSelected
                    label: "ONLINE"
                    value: qsTr("%1 台").arg(root.bridge.onlineCount)
                }

                // 單機
                Cell {
                    visible: root.bridge.isSingleSelected
                    label: "TARGET"
                    value: root.bridge.selectedAgent.replace(/_agent$/, "").toUpperCase()
                    valueColor: Theme.accent
                    tooltip: root.bridge.selectedAgent
                }
                Cell {
                    visible: root.bridge.isSingleSelected && !root.hasLiveTelemetry
                    label: "STATUS"
                    value: root.summary.isWaitingForFlightController ? qsTr("等待飛控連線中...")
                                                                    : qsTr("等待遙測資料...")
                    valueColor: root.summary.isWaitingForFlightController ? Theme.caution : Theme.textMuted
                }
                Cell {
                    visible: root.hasLiveTelemetry
                    label: "MODE"
                    value: root.summary.modeLabel || ""
                    valueColor: Theme.normal
                    isBoxed: true
                }
                Cell {
                    visible: root.hasLiveTelemetry
                    label: "BATT"
                    value: root.withUnit(root.summary.batteryText, "%") + " "
                           + root.withUnit(root.summary.voltageText, "V") + " "
                           + root.withUnit(root.summary.currentText, "A")
                    isStyled: true
                }
                // PX4 原生飛行模式（nav_state）
                Cell {
                    visible: root.hasLiveTelemetry
                    label: "PX4 MODE"
                    value: root.summary.px4ModeLabel || ""
                    valueColor: root.summary.isPx4Valid ? Theme.normal : Theme.caution
                }
                Cell {
                    visible: root.hasLiveTelemetry
                    label: "PX4"
                    value: root.summary.px4Label || ""
                    valueColor: root.summary.isPx4Valid ? Theme.textValue : Theme.caution
                }
                Cell {
                    visible: root.hasLiveTelemetry
                    label: "NED  N / E / D"
                    value: root.summary.nedText || ""
                }
            }
        }

        Separator {}
        // DDS 連線狀態
        Cell {
            label: "LINK"
            value: root.bridge.isLinkConnected ? "DDS" : qsTr("中斷")
            valueColor: root.bridge.isLinkConnected ? Theme.normal : Theme.danger
            tooltip: (root.bridge.isLinkConnected ? qsTr("已連線") : qsTr("未連線"))
                     + (root.bridge.linkMessage.length > 0 ? "：" + root.bridge.linkMessage : "")
            Layout.fillHeight: true
            Accessible.role: Accessible.Indicator
            Accessible.name: tooltip
        }
        // UTC+8 時鐘（台灣時間）：GCS 電腦系統時間換算為 UTC 後固定加 8 小時，不受電腦時區設定影響
        Cell {
            id: clockCell

            /// 相對 UTC 的固定時差（小時）
            readonly property int utcOffsetHours: 8

            label: "UTC+" + utcOffsetHours
            value: "--:--:--"
            tooltip: qsTr("台灣時間（UTC+8），取自本機系統時鐘；日誌檔時間為 UTC")
            Layout.fillHeight: true

            Timer {
                interval: 1000
                running: true
                repeat: true
                triggeredOnStart: true
                onTriggered: {
                    const shifted = new Date(Date.now() + clockCell.utcOffsetHours * 3600 * 1000)
                    clockCell.value = shifted.toISOString().substr(11, 8)
                }
            }
        }

        ActionButton {
            iconName: "settings"
            accentColor: Theme.textMuted
            implicitWidth: 48
            implicitHeight: 47
            padding: 0
            background: Rectangle { color: parent.hovered ? Theme.controlHover : "transparent" }
            tooltip: qsTr("進階設定")
            Accessible.name: qsTr("開啟進階設定")
            onClicked: root.settingsRequested()
        }
    }

    /// 儀表格：上方小字標籤、下方等寬數值，右側細分隔線
    component Cell: Item {
        id: cell

        property string label: ""
        property string value: ""
        property color valueColor: Theme.textValue
        /// 數值加外框（目前模式，比照 PFD 的 FMA 方框）
        property bool isBoxed: false
        /// 數值含 StyledText 標記（單位以暗色顯示）
        property bool isStyled: false
        property string tooltip: ""

        implicitWidth: visible ? Math.max(labelText.implicitWidth, valueBox.width) + 2 * Theme.spaceLg : 0
        height: parent ? parent.height : 47

        Column {
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.spaceLg
            spacing: 2

            Label {
                id: labelText

                text: cell.label
                color: Theme.textMuted
                font.pixelSize: Theme.fontLabel
                font.letterSpacing: Theme.labelSpacing
            }
            Rectangle {
                id: valueBox

                width: valueText.implicitWidth + (cell.isBoxed ? 10 : 0)
                height: valueText.implicitHeight
                x: cell.isBoxed ? -5 : 0
                color: "transparent"
                border.width: cell.isBoxed ? 1 : 0
                border.color: cell.valueColor

                Label {
                    id: valueText

                    anchors.centerIn: parent
                    text: cell.value
                    textFormat: cell.isStyled ? Text.StyledText : Text.PlainText
                    color: cell.valueColor
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontBase
                }
            }
        }
        Rectangle {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: 1
            height: parent.height
            color: Theme.borderSoft
        }
        HoverHandler { id: cellHover }
        ToolTip.visible: cell.tooltip.length > 0 && cellHover.hovered
        ToolTip.text: cell.tooltip
        ToolTip.delay: 600
    }

    component Separator: Rectangle {
        implicitWidth: 1
        Layout.fillHeight: true
        color: Theme.border
    }
}
