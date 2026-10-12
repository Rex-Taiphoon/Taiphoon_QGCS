/**
 * @file StatusBar.qml
 * @brief 頂部狀態列：品牌圖示、選取目標的遙測摘要、DDS 連線燈號與設定入口（網頁版 StatusBar）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
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

    implicitHeight: 60
    color: Theme.bar

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.borderSoft
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 18
        anchors.rightMargin: 18
        anchors.topMargin: 4
        anchors.bottomMargin: 4
        spacing: 14

        Rectangle {
            implicitWidth: 50
            implicitHeight: 50
            radius: 6
            color: Theme.textStrong

            Image {
                anchors.fill: parent
                anchors.margins: 2
                source: "qrc:/qt/qml/TaiphoonQgcs/resources/taiphoon-logo.png"
                fillMode: Image.PreserveAspectFit
                Accessible.name: "Taiphoon"
            }
        }

        // 摘要可水平捲動，燈號與設定入口保持可見
        Flickable {
            id: summaryArea

            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: summaryRow.implicitWidth
            contentHeight: height
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            ScrollBar.horizontal: ScrollBar { height: 4 }

            Row {
                id: summaryRow

                height: summaryArea.height
                spacing: 14

                // 全體
                SummaryText {
                    visible: root.bridge.isAllSelected
                    text: qsTr("機群數量：%1 台").arg(root.bridge.onlineCount)
                    color: "white"
                    font.bold: true
                }
                SummaryText {
                    visible: root.bridge.isAllSelected && root.bridge.onlineCount === 0
                    text: qsTr("編隊：等待回報")
                    color: Theme.textValue
                    font.pixelSize: Theme.fontSmall
                }
                Repeater {
                    // 目前資料鏈尚未轉送 FormationEpoch，與網頁版相同顯示「待回報」
                    model: root.bridge.isAllSelected ? root.bridge.onlineAgents : []
                    delegate: SummaryText {
                        required property string modelData
                        text: modelData.replace(/^uav_(\d+)_agent$/, "$1") + " " + qsTr("待回報")
                        color: Theme.textValue
                        font.pixelSize: Theme.fontSmall
                    }
                }

                // 單機
                SummaryText {
                    visible: root.bridge.isSingleSelected
                    text: root.bridge.selectedAgent
                    color: "white"
                    font.bold: true
                }
                SummaryText {
                    visible: root.bridge.isSingleSelected
                             && (root.summary.isWaitingForFlightController || !root.summary.hasTelemetry)
                    text: root.summary.isWaitingForFlightController ? qsTr("等待飛控連線中...")
                                                                    : qsTr("等待遙測資料...")
                    color: root.summary.isWaitingForFlightController ? Theme.warning : Theme.textMuted
                    font.bold: root.summary.isWaitingForFlightController === true
                }
                Repeater {
                    model: root.bridge.isSingleSelected && root.summary.hasTelemetry
                           && !root.summary.isWaitingForFlightController
                           ? [
                                 {"text": "|", "color": "#556677", "bold": false},
                                 {"text": root.summary.modeLabel, "color": Theme.warning, "bold": true},
                                 {"text": "|", "color": "#556677", "bold": false},
                                 {"text": qsTr("電量: %1%").arg(root.summary.batteryText), "color": Theme.textValue, "bold": false},
                                 {"text": "|", "color": "#556677", "bold": false},
                                 {"text": "PX4: " + root.summary.px4Label,
                                  "color": root.summary.isPx4Valid ? Theme.textValue : Theme.warning,
                                  "bold": !root.summary.isPx4Valid},
                                 {"text": "|", "color": "#556677", "bold": false},
                                 {"text": "NED: " + root.summary.nedText, "color": Theme.textValue, "bold": false}
                             ]
                           : []
                    delegate: SummaryText {
                        required property var modelData
                        text: modelData.text
                        color: modelData.color
                        font.bold: modelData.bold
                    }
                }
            }
        }

        // DDS 連線燈號
        Rectangle {
            implicitWidth: 16
            implicitHeight: 16
            radius: 8
            color: root.bridge.isLinkConnected ? Theme.online : Theme.danger
            Accessible.role: Accessible.Indicator
            Accessible.name: connectionTip.text

            HoverHandler { id: dotHover }
            ToolTip {
                id: connectionTip
                visible: dotHover.hovered
                text: (root.bridge.isLinkConnected ? qsTr("已連線") : qsTr("未連線"))
                      + (root.bridge.linkMessage.length > 0 ? "：" + root.bridge.linkMessage : "")
            }
        }

        ActionButton {
            iconName: "settings"
            accentColor: Theme.textMuted
            implicitWidth: 44
            implicitHeight: 44
            padding: 0
            tooltip: qsTr("進階設定")
            Accessible.name: qsTr("開啟進階設定")
            onClicked: root.settingsRequested()
        }
    }

    component SummaryText: Label {
        height: parent ? parent.height : implicitHeight
        verticalAlignment: Text.AlignVCenter
        font.pixelSize: Theme.fontBase
    }
}
