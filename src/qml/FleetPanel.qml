/**
 * @file FleetPanel.qml
 * @brief 機群懸浮視窗：選取單機或全體、線上台數、圓角卡片、可最小化（網頁版 FleetPanel）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property var bridge
    required property var settings

    property bool isCollapsed: false

    width: isCollapsed ? header.implicitWidth + 32 : 380
    height: isCollapsed ? header.implicitHeight + 24 : Math.min(header.implicitHeight + 24 + content.implicitHeight + 16,
                                                               maximumHeight)
    /// 由父項目設定可用高度，保留底部導覽控制空間
    property real maximumHeight: 600
    color: Theme.panel
    radius: 16
    border.color: "#405568"
    clip: true

    RowLayout {
        id: header

        x: 16
        y: 12
        width: root.isCollapsed ? implicitWidth : root.width - 32
        spacing: 12

        Label {
            text: qsTr("機群管理")
            color: Theme.textStrong
            font.pixelSize: 22
            font.bold: true
        }
        Label {
            text: qsTr("%1 台").arg(root.bridge.onlineCount)
            color: Theme.textMuted
            font.pixelSize: Theme.fontSmall
            Layout.fillWidth: !root.isCollapsed
            horizontalAlignment: Text.AlignRight
        }
        ActionButton {
            iconName: root.isCollapsed ? "chevronDown" : "minus"
            accentColor: Theme.textMuted
            implicitWidth: 36
            implicitHeight: 36
            padding: 0
            tooltip: root.isCollapsed ? qsTr("展開機群管理") : qsTr("最小化機群管理")
            Accessible.name: tooltip
            onClicked: root.isCollapsed = !root.isCollapsed
        }
    }

    Flickable {
        id: content

        visible: !root.isCollapsed
        anchors.top: header.bottom
        anchors.topMargin: 10
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 12
        implicitHeight: list.implicitHeight
        contentHeight: list.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        ColumnLayout {
            id: list

            width: content.width
            spacing: 8

            // 全體
            CardButton {
                isActive: root.bridge.isAllSelected
                Accessible.name: qsTr("選取全部機群，共 %1 台").arg(root.bridge.onlineCount)
                onClicked: root.bridge.selectAgent("all")
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10
                    Icon { name: "hexagon"; color: Theme.accent; implicitWidth: 22; implicitHeight: 22 }
                    Label { text: qsTr("機群數量"); color: Theme.text; font.pixelSize: Theme.fontBase; Layout.fillWidth: true }
                    Label { text: qsTr("%1 台").arg(root.bridge.onlineCount); color: Theme.textMuted; font.pixelSize: Theme.fontBase }
                }
            }

            Rectangle { color: Theme.borderSoft; implicitHeight: 1; Layout.fillWidth: true }

            Label {
                text: "● " + qsTr("線上")
                color: Theme.online
                font.pixelSize: Theme.fontSmall
            }
            Label {
                visible: root.bridge.onlineCount === 0
                text: qsTr("等待 Agent 上線...")
                color: Theme.textMuted
                font.pixelSize: Theme.fontBase
            }

            Repeater {
                model: root.bridge.fleetModel
                delegate: CardButton {
                    id: card

                    required property string agent
                    required property bool isSelected
                    required property bool isWaitingForFlightController
                    required property string modeLabel
                    required property string batteryText
                    required property string voltageText
                    required property string currentText
                    required property string altitudeText
                    required property bool hasAttitude
                    required property real roll
                    required property real pitch

                    isActive: isSelected
                    implicitHeight: cardRow.implicitHeight + 24
                    Accessible.name: qsTr("選取 %1").arg(agent)
                    onClicked: root.bridge.selectAgent(agent)

                    RowLayout {
                        id: cardRow

                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 10

                        Label { text: "●"; color: Theme.online; font.pixelSize: 14; Layout.alignment: Qt.AlignTop }

                        ColumnLayout {
                            spacing: 4
                            Layout.fillWidth: true

                            Label {
                                text: card.agent
                                color: Theme.textStrong
                                font.pixelSize: Theme.fontBase
                                font.bold: true
                            }
                            Label {
                                text: card.isWaitingForFlightController
                                      ? qsTr("等待飛控連線中...")
                                      : card.modeLabel + " · " + qsTr("電量 %1%").arg(card.batteryText)
                                color: card.isWaitingForFlightController ? Theme.warning : Theme.textMuted
                                font.pixelSize: Theme.fontSmall
                            }
                            GridLayout {
                                visible: !card.isWaitingForFlightController
                                columns: 4
                                columnSpacing: 6
                                rowSpacing: 2

                                Label { text: "🔋"; font.pixelSize: 14; color: Theme.textMuted }
                                Label { text: "Volt"; color: Theme.textMuted; font.pixelSize: 15 }
                                Label { text: card.voltageText; color: Theme.textValue; font.pixelSize: 15; horizontalAlignment: Text.AlignRight; Layout.minimumWidth: 48 }
                                Label { text: "V"; color: Theme.textMuted; font.pixelSize: 15 }

                                Label { text: "⚡"; font.pixelSize: 14; color: Theme.textMuted }
                                Label { text: "Curr"; color: Theme.textMuted; font.pixelSize: 15 }
                                Label { text: card.currentText; color: Theme.textValue; font.pixelSize: 15; horizontalAlignment: Text.AlignRight; Layout.minimumWidth: 48 }
                                Label { text: "A"; color: Theme.textMuted; font.pixelSize: 15 }

                                Label { text: "↕"; font.pixelSize: 14; color: Theme.textMuted }
                                Label { text: "Alt"; color: Theme.textMuted; font.pixelSize: 15 }
                                Label { text: card.altitudeText; color: Theme.textValue; font.pixelSize: 15; horizontalAlignment: Text.AlignRight; Layout.minimumWidth: 48 }
                                Label { text: "m"; color: Theme.textMuted; font.pixelSize: 15 }
                            }
                        }

                        Item {
                            visible: root.settings.showAttitude && !card.isWaitingForFlightController
                            implicitWidth: 72
                            implicitHeight: 72

                            AttitudeIndicator {
                                anchors.fill: parent
                                visible: card.hasAttitude
                                roll: card.roll
                                pitch: card.pitch
                            }
                            Label {
                                anchors.centerIn: parent
                                visible: !card.hasAttitude
                                text: qsTr("等待姿態")
                                color: Theme.textMuted
                                font.pixelSize: 13
                            }
                        }
                    }
                }
            }
        }
    }

    component CardButton: AbstractButton {
        id: cardButton

        /// 是否為目前選取（不可命名為 isSelected，避免與 delegate 的模型角色同名而遮蔽）
        property bool isActive: false

        implicitHeight: 52
        Layout.fillWidth: true
        hoverEnabled: true
        Accessible.role: Accessible.Button

        background: Rectangle {
            radius: 12
            color: cardButton.isActive ? Theme.controlActive : (cardButton.hovered ? "#16232f" : Theme.panelDeep)
            border.width: cardButton.visualFocus || cardButton.isActive ? 2 : 1
            border.color: cardButton.isActive || cardButton.visualFocus ? Theme.accent : Theme.borderSoft
        }
    }
}
