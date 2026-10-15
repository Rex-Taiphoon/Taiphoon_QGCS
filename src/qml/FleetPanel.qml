/**
 * @file FleetPanel.qml
 * @brief 機群懸浮視窗：表格列出各機模式、電量、高度，選取列展開電壓、電流與姿態，可最小化（網頁版 FleetPanel）
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
    /// 由父項目設定可用高度，保留底部導覽控制空間
    property real maximumHeight: 600

    /// 表格欄寬：模式、電量、高度（其餘給機號）
    readonly property int modeColumnWidth: 112
    readonly property int numberColumnWidth: 58

    width: isCollapsed ? header.implicitWidth + 2 * Theme.spaceLg : 360
    height: isCollapsed ? 44 : Math.min(44 + 1 + content.implicitHeight, maximumHeight)
    color: Theme.panel
    radius: Theme.radius
    border.color: Theme.border
    clip: true

    RowLayout {
        id: header

        x: Theme.spaceLg
        width: root.isCollapsed ? implicitWidth : root.width - Theme.spaceLg - Theme.spaceXs
        height: 44
        spacing: Theme.spaceMd

        Label {
            text: qsTr("機群")
            color: Theme.textMuted
            font.pixelSize: Theme.fontLabel
            font.letterSpacing: Theme.labelSpacing
        }
        Label {
            text: qsTr("%1 台線上").arg(root.bridge.onlineCount)
            color: root.bridge.onlineCount > 0 ? Theme.normal : Theme.textMuted
            font.family: Theme.fontMono
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
            background: Item {}
            tooltip: root.isCollapsed ? qsTr("展開機群管理") : qsTr("最小化機群管理")
            Accessible.name: tooltip
            onClicked: root.isCollapsed = !root.isCollapsed
        }
    }

    Rectangle {
        visible: !root.isCollapsed
        y: 44
        width: parent.width
        height: 1
        color: Theme.border
    }

    Flickable {
        id: content

        visible: !root.isCollapsed
        anchors.top: parent.top
        anchors.topMargin: 45
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 1
        implicitHeight: list.implicitHeight
        contentHeight: list.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {}

        ColumnLayout {
            id: list

            width: content.width
            spacing: 0

            // 欄位標題
            TableRow {
                implicitHeight: 30
                cells: [qsTr("機號"), qsTr("模式"), qsTr("電量"), qsTr("高度 m")]
                cellColor: Theme.textMuted
                cellSize: Theme.fontLabel
                isMono: false
            }

            // 全體
            RowButton {
                isActive: root.bridge.isAllSelected
                Accessible.name: qsTr("選取全部機群，共 %1 台").arg(root.bridge.onlineCount)
                onClicked: root.bridge.selectAgent("all")
                TableRow {
                    anchors.fill: parent
                    cells: [qsTr("全部"), qsTr("%1 台").arg(root.bridge.onlineCount), "—", "—"]
                    firstColor: Theme.accent
                    cellColor: Theme.textMuted
                }
            }

            Label {
                visible: root.bridge.onlineCount === 0
                text: qsTr("等待 Agent 上線...")
                color: Theme.textMuted
                font.pixelSize: Theme.fontSmall
                padding: Theme.spaceLg
            }

            Repeater {
                model: root.bridge.fleetModel
                delegate: RowButton {
                    id: row

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

                    readonly property bool isExpanded: isSelected && !isWaitingForFlightController
                    /// 表格顯示短名稱（去掉固定的 _agent 字尾），完整名稱見提示與無障礙名稱
                    readonly property string shortName: agent.replace(/_agent$/, "")

                    isActive: isSelected
                    implicitHeight: Theme.rowHeight + (isExpanded ? detail.implicitHeight : 0)
                    Accessible.name: qsTr("選取 %1").arg(agent)
                    ToolTip.visible: hovered
                    ToolTip.text: agent
                    ToolTip.delay: 600
                    onClicked: root.bridge.selectAgent(agent)

                    TableRow {
                        width: parent.width
                        implicitHeight: Theme.rowHeight
                        cells: row.isWaitingForFlightController
                               ? [row.shortName, qsTr("等待飛控"), "—", "—"]
                               : [row.shortName, row.modeLabel, row.batteryText + "%", row.altitudeText]
                        firstColor: Theme.textStrong
                        secondColor: row.isWaitingForFlightController ? Theme.caution : Theme.normal
                    }

                    // 選取列展開：電壓、電流與姿態
                    RowLayout {
                        id: detail

                        visible: row.isExpanded
                        y: Theme.rowHeight
                        x: Theme.spaceLg
                        width: parent.width - 2 * Theme.spaceLg
                        implicitHeight: Math.max(values.implicitHeight, attitude.implicitHeight) + Theme.spaceMd
                        spacing: Theme.spaceLg

                        Item {
                            id: attitude

                            visible: root.settings.showAttitude
                            implicitWidth: 72
                            implicitHeight: 72
                            Layout.alignment: Qt.AlignTop

                            AttitudeIndicator {
                                anchors.fill: parent
                                visible: row.hasAttitude
                                roll: row.roll
                                pitch: row.pitch
                            }
                            Label {
                                anchors.centerIn: parent
                                visible: !row.hasAttitude
                                text: qsTr("等待姿態")
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontLabel
                            }
                        }
                        GridLayout {
                            id: values

                            columns: 2
                            columnSpacing: Theme.spaceLg
                            rowSpacing: 2
                            Layout.alignment: Qt.AlignTop
                            Layout.fillWidth: true

                            DetailLabel { text: qsTr("電壓") }
                            DetailValue { text: row.voltageText + " V" }
                            DetailLabel { text: qsTr("電流") }
                            DetailValue { text: row.currentText + " A" }
                            DetailLabel { visible: row.hasAttitude; text: "ROLL" }
                            DetailValue { visible: row.hasAttitude; text: row.roll.toFixed(1) + "°" }
                            DetailLabel { visible: row.hasAttitude; text: "PITCH" }
                            DetailValue { visible: row.hasAttitude; text: row.pitch.toFixed(1) + "°" }
                        }
                    }
                }
            }
        }
    }

    /// 可點選的表格列：選取時以強調色細框標示
    component RowButton: AbstractButton {
        id: rowButton

        /// 是否為目前選取（不可命名為 isSelected，避免與 delegate 的模型角色同名而遮蔽）
        property bool isActive: false

        implicitHeight: Theme.rowHeight
        Layout.fillWidth: true
        hoverEnabled: true
        Accessible.role: Accessible.Button

        background: Rectangle {
            color: rowButton.isActive ? Theme.controlActive
                                      : (rowButton.hovered ? Theme.controlHover : "transparent")
            border.width: rowButton.isActive || rowButton.visualFocus ? 1 : 0
            border.color: Theme.accent

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                visible: !rowButton.isActive
                color: Theme.borderSoft
            }
        }
    }

    /// 四欄表格列：機號、模式、電量、高度（後兩欄靠右）
    component TableRow: RowLayout {
        property var cells: ["", "", "", ""]
        property color cellColor: Theme.textValue
        property color firstColor: cellColor
        property color secondColor: cellColor
        property int cellSize: Theme.fontSmall
        property bool isMono: true

        spacing: Theme.spaceSm
        Layout.fillWidth: true

        Label {
            text: parent.cells[0]
            color: parent.firstColor
            font.family: parent.isMono ? Theme.fontMono : Qt.application.font.family
            font.pixelSize: parent.cellSize
            font.letterSpacing: parent.isMono ? 0 : Theme.labelSpacing
            elide: Text.ElideRight
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spaceLg
        }
        Label {
            text: parent.cells[1]
            color: parent.secondColor
            font.pixelSize: parent.cellSize
            font.letterSpacing: parent.isMono ? 0 : Theme.labelSpacing
            elide: Text.ElideRight
            Layout.preferredWidth: root.modeColumnWidth
        }
        Label {
            text: parent.cells[2]
            color: parent.cellColor
            font.family: parent.isMono ? Theme.fontMono : Qt.application.font.family
            font.pixelSize: parent.cellSize
            font.letterSpacing: parent.isMono ? 0 : Theme.labelSpacing
            horizontalAlignment: Text.AlignRight
            Layout.preferredWidth: root.numberColumnWidth
        }
        Label {
            text: parent.cells[3]
            color: parent.cellColor
            font.family: parent.isMono ? Theme.fontMono : Qt.application.font.family
            font.pixelSize: parent.cellSize
            font.letterSpacing: parent.isMono ? 0 : Theme.labelSpacing
            horizontalAlignment: Text.AlignRight
            Layout.preferredWidth: root.numberColumnWidth
            Layout.rightMargin: Theme.spaceLg
        }
    }

    component DetailLabel: Label {
        color: Theme.textMuted
        font.pixelSize: Theme.fontLabel
        font.letterSpacing: Theme.labelSpacing
    }

    component DetailValue: Label {
        color: Theme.textValue
        font.family: Theme.fontMono
        font.pixelSize: Theme.fontSmall
    }
}
