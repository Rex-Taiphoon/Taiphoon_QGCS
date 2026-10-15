/**
 * @file StatusBar.qml
 * @brief 頂部狀態列：品牌、選取目標的遙測儀表格、DDS 連線狀態與設定入口（網頁版 StatusBar）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 每個欄位為「標籤在上、數值在下」的儀表格，以細分隔線區隔；數值一律使用等寬字。
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

        // 品牌：Logo 原圖為白底，以白色方塊承載並縮小
        RowLayout {
            spacing: Theme.spaceSm
            Layout.leftMargin: Theme.spaceLg
            Layout.rightMargin: Theme.spaceLg

            Rectangle {
                implicitWidth: 30
                implicitHeight: 30
                color: Theme.textStrong

                Image {
                    anchors.fill: parent
                    anchors.margins: 1
                    source: "qrc:/qt/qml/TaiphoonQgcs/resources/taiphoon-logo.png"
                    fillMode: Image.PreserveAspectFit
                    mipmap: true
                    Accessible.name: "Taiphoon"
                }
            }
            Label {
                text: "TAIPHOON"
                color: Theme.textStrong
                font.pixelSize: Theme.fontSmall
                font.bold: true
                font.letterSpacing: 3
            }
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
                    label: qsTr("目標")
                    value: qsTr("全部機群")
                    valueColor: Theme.accent
                }
                Cell {
                    visible: root.bridge.isAllSelected
                    label: qsTr("線上")
                    value: qsTr("%1 台").arg(root.bridge.onlineCount)
                }
                Cell {
                    visible: root.bridge.isAllSelected
                    label: qsTr("編隊")
                    // 目前資料鏈尚未轉送 FormationEpoch，與網頁版相同顯示「待回報」
                    value: root.bridge.onlineCount === 0 ? qsTr("等待回報")
                                                         : root.bridge.onlineAgents
                                                               .map(a => a.replace(/^uav_(\d+)_agent$/, "$1"))
                                                               .join(" ") + " " + qsTr("待回報")
                    valueColor: Theme.textMuted
                }

                // 單機
                Cell {
                    visible: root.bridge.isSingleSelected
                    label: qsTr("目標")
                    value: root.bridge.selectedAgent
                    valueColor: Theme.accent
                }
                Cell {
                    visible: root.bridge.isSingleSelected && !root.hasLiveTelemetry
                    label: qsTr("狀態")
                    value: root.summary.isWaitingForFlightController ? qsTr("等待飛控連線中...")
                                                                    : qsTr("等待遙測資料...")
                    valueColor: root.summary.isWaitingForFlightController ? Theme.caution : Theme.textMuted
                }
                Cell {
                    visible: root.hasLiveTelemetry
                    label: qsTr("模式")
                    value: root.summary.modeLabel || ""
                    valueColor: Theme.normal
                }
                Cell {
                    visible: root.hasLiveTelemetry
                    label: qsTr("電量")
                    value: (root.summary.batteryText || "—") + " %"
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
            label: qsTr("連線")
            value: root.bridge.isLinkConnected ? "DDS" : qsTr("中斷")
            valueColor: root.bridge.isLinkConnected ? Theme.normal : Theme.danger
            Layout.fillHeight: true
            Accessible.role: Accessible.Indicator
            Accessible.name: connectionTip.text

            HoverHandler { id: linkHover }
            ToolTip {
                id: connectionTip
                visible: linkHover.hovered
                text: (root.bridge.isLinkConnected ? qsTr("已連線") : qsTr("未連線"))
                      + (root.bridge.linkMessage.length > 0 ? "：" + root.bridge.linkMessage : "")
            }
        }
        Separator {}

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

        implicitWidth: visible ? Math.max(labelText.implicitWidth, valueText.implicitWidth) + 2 * Theme.spaceLg : 0
        height: parent ? parent.height : 47

        Column {
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.spaceLg
            spacing: 1

            Label {
                id: labelText

                text: cell.label
                color: Theme.textMuted
                font.pixelSize: Theme.fontLabel
                font.letterSpacing: Theme.labelSpacing
            }
            Label {
                id: valueText

                text: cell.value
                color: cell.valueColor
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontBase
            }
        }
        Rectangle {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: 1
            height: parent.height
            color: Theme.borderSoft
        }
    }

    component Separator: Rectangle {
        implicitWidth: 1
        Layout.fillHeight: true
        color: Theme.border
    }
}
