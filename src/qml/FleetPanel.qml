/**
 * @file FleetPanel.qml
 * @brief 機群懸浮視窗：表格列出各機模式、電量、高度，下方固定顯示選取機的姿態儀與電力，可最小化（網頁版 FleetPanel）
 * @author Taiphoon GCS Team
 * @date 2026-10-15
 *
 * 姿態區固定在表格下方，不隨選取列展開，切換選取時表格高度不跳動（航電座艙提案）。
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
    readonly property int headerHeight: 40

    readonly property var summary: bridge.selectedSummary
    /// 選取單機且已有遙測時顯示姿態區
    readonly property bool hasSelectedTelemetry: bridge.isSingleSelected && summary.hasTelemetry === true
                                                 && summary.isWaitingForFlightController !== true

    /**
     * @brief 帶正負號的角度文字（負號使用 U+2212）
     * @param value 角度（度）
     * @return 例如 "+1.6°"、"−8.0°"
     */
    function signedDegrees(value) {
        const text = Math.abs(value).toFixed(1)
        return (value < 0 && Number(text) !== 0 ? "−" : "+") + text + "°"
    }

    width: isCollapsed ? header.implicitWidth + 2 * Theme.spaceLg : 360
    height: isCollapsed ? headerHeight : Math.min(headerHeight + 1 + content.implicitHeight, maximumHeight)
    color: Theme.panel
    radius: Theme.radius
    border.color: Theme.border
    clip: true

    RowLayout {
        id: header

        x: Theme.spaceLg
        width: root.isCollapsed ? implicitWidth : root.width - Theme.spaceLg - Theme.spaceXs
        height: root.headerHeight
        spacing: Theme.spaceMd

        Label {
            text: "FLEET"
            color: Theme.textBody
            font.pixelSize: Theme.fontLabel + 1
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
            implicitWidth: 32
            implicitHeight: 32
            padding: 0
            background: Item {}
            tooltip: root.isCollapsed ? qsTr("展開機群管理") : qsTr("最小化機群管理")
            Accessible.name: tooltip
            onClicked: root.isCollapsed = !root.isCollapsed
        }
    }

    Rectangle {
        visible: !root.isCollapsed
        y: root.headerHeight
        width: parent.width
        height: 1
        color: Theme.border
    }

    Flickable {
        id: content

        visible: !root.isCollapsed
        anchors.top: parent.top
        anchors.topMargin: root.headerHeight + 1
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
                cells: ["ID", "MODE", "BATT", "ALT M"]
                cellColor: Theme.textMuted
                cellSize: Theme.fontLabel
                isMono: false
            }
            Rectangle { implicitHeight: 1; Layout.fillWidth: true; color: Theme.border }

            // 全體
            RowButton {
                isActive: root.bridge.isAllSelected
                Accessible.name: qsTr("選取全部機群，共 %1 台").arg(root.bridge.onlineCount)
                onClicked: root.bridge.selectAgent("all")
                TableRow {
                    anchors.fill: parent
                    cells: [qsTr("全部"), qsTr("%1 台").arg(root.bridge.onlineCount), "—", "—"]
                    firstColor: Theme.accent
                    cellColor: Theme.textDim
                    secondColor: Theme.textMuted
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
                    required property string altitudeText

                    /// 表格顯示大寫短名稱（去掉固定的 _agent 字尾），完整名稱見提示與無障礙名稱
                    readonly property string shortName: agent.replace(/_agent$/, "").toUpperCase()

                    isActive: isSelected
                    Accessible.name: qsTr("選取 %1").arg(agent)
                    ToolTip.visible: hovered
                    ToolTip.text: agent
                    ToolTip.delay: 600
                    onClicked: root.bridge.selectAgent(agent)

                    TableRow {
                        anchors.fill: parent
                        cells: row.isWaitingForFlightController
                               ? [row.shortName, qsTr("等待飛控"), "—", "—"]
                               : [row.shortName, row.modeLabel, row.batteryText + "%", row.altitudeText]
                        firstColor: Theme.textStrong
                        secondColor: row.isWaitingForFlightController ? Theme.caution : Theme.normal
                        cellColor: row.isWaitingForFlightController ? Theme.textDim : Theme.textValue
                    }
                }
            }

            // 選取機的姿態與電力：固定在表格下方
            Rectangle {
                visible: root.hasSelectedTelemetry
                implicitHeight: 1
                Layout.fillWidth: true
                color: Theme.border
            }
            RowLayout {
                visible: root.hasSelectedTelemetry
                spacing: Theme.spaceLg
                Layout.fillWidth: true
                Layout.margins: Theme.spaceMd
                Layout.leftMargin: Theme.spaceLg

                Item {
                    visible: root.settings.showAttitude
                    implicitWidth: 88
                    implicitHeight: 88

                    AttitudeIndicator {
                        anchors.fill: parent
                        visible: root.summary.hasAttitude === true
                        roll: root.summary.roll || 0
                        pitch: root.summary.pitch || 0
                    }
                    Label {
                        anchors.centerIn: parent
                        visible: root.summary.hasAttitude !== true
                        text: qsTr("等待姿態")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontLabel
                    }
                }
                GridLayout {
                    columns: 2
                    columnSpacing: Theme.spaceLg
                    rowSpacing: Theme.spaceXs
                    Layout.alignment: Qt.AlignVCenter
                    Layout.fillWidth: true

                    DetailLabel { visible: root.summary.hasAttitude === true; text: "ROLL" }
                    DetailValue {
                        visible: root.summary.hasAttitude === true
                        text: root.signedDegrees(root.summary.roll || 0)
                    }
                    DetailLabel { visible: root.summary.hasAttitude === true; text: "PITCH" }
                    DetailValue {
                        visible: root.summary.hasAttitude === true
                        text: root.signedDegrees(root.summary.pitch || 0)
                    }
                    DetailLabel { visible: root.summary.hasHeading === true; text: "HDG" }
                    DetailValue {
                        visible: root.summary.hasHeading === true
                        text: String(Math.round(root.summary.heading || 0) % 360).padStart(3, "0") + "°"
                    }
                    DetailLabel { text: "VOLT" }
                    DetailValue { text: (root.summary.voltageText || "—") + " V" }
                    DetailLabel { text: "CURR" }
                    DetailValue { text: (root.summary.currentText || "—") + " A" }
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
            font.family: parent.isMono ? Theme.fontMono : Theme.fontFamily
            font.pixelSize: parent.cellSize
            font.letterSpacing: parent.isMono ? 0 : Theme.labelSpacing
            elide: Text.ElideRight
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spaceLg
        }
        Label {
            text: parent.cells[1]
            color: parent.secondColor
            font.family: Theme.fontFamily
            font.pixelSize: parent.cellSize
            font.letterSpacing: parent.isMono ? 0 : Theme.labelSpacing
            elide: Text.ElideRight
            Layout.preferredWidth: root.modeColumnWidth
        }
        Label {
            text: parent.cells[2]
            color: parent.cellColor
            font.family: parent.isMono ? Theme.fontMono : Theme.fontFamily
            font.pixelSize: parent.cellSize
            font.letterSpacing: parent.isMono ? 0 : Theme.labelSpacing
            horizontalAlignment: Text.AlignRight
            Layout.preferredWidth: root.numberColumnWidth
        }
        Label {
            text: parent.cells[3]
            color: parent.cellColor
            font.family: parent.isMono ? Theme.fontMono : Theme.fontFamily
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
