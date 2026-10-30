/**
 * @file FleetPanel.qml
 * @brief 機群懸浮視窗：表格列出各機模式（Agent 任務階段與 PX4 飛行模式）、電量、高度，下方固定顯示選取機的姿態儀與電力，可最小化（網頁版 FleetPanel）
 * @author Taiphoon GCS Team
 * @date 2026-10-15
 *
 * 姿態區固定在表格下方，不隨選取列展開，切換選取時表格高度不跳動（航電座艙提案）。
 * Agent 的健康狀態（PX4 連線、預檢）顯示在下方明細，資料中斷或預檢未通過時機號旁加警示標記。
 * 在單機列或「全部」按右鍵可設定影像串流網址（單機網址或機群網址，ADR-0019）；右鍵不改變選取。
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property var bridge
    required property var settings

    property bool isCollapsed: false
    /// 此版本含影像播放時才提供右鍵設定影像串流
    property bool isVideoAvailable: false
    /// 由父項目設定可用高度，保留底部導覽控制空間
    property real maximumHeight: 600

    /// 表格欄寬：模式、電量、高度（其餘給機號）
    readonly property int modeColumnWidth: 112
    readonly property int numberColumnWidth: 58
    /// 模式欄顯示兩行（Agent 任務階段＋PX4 飛行模式）時的列高
    readonly property int twoLineRowHeight: 44
    readonly property int headerHeight: 40

    readonly property var summary: bridge.selectedSummary
    /// 選取單機且已有遙測時顯示姿態區
    readonly property bool hasSelectedTelemetry: bridge.isSingleSelected && summary.hasTelemetry === true
                                                 && summary.isWaitingForFlightController !== true
    /// 選取單機且已收到健康狀態時顯示 PX4 連線與預檢（等待飛控時也顯示，才看得出原因）
    readonly property bool hasSelectedHealth: bridge.isSingleSelected && summary.hasHealth === true

    /**
     * @brief 要求開啟影像串流網址設定
     * @param agent uav_N_agent；空字串表示機群網址
     */
    signal videoSourceRequested(string agent)

    /**
     * @brief 在游標位置開啟列的右鍵選單
     * @param agent uav_N_agent；空字串表示「全部」
     */
    function openRowMenu(agent) {
        rowMenu.agent = agent
        rowMenu.popup()
    }

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
                cells: ["ID", "MODE", "BATT", "ALT"]
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
                TapHandler {
                    enabled: root.isVideoAvailable
                    acceptedButtons: Qt.RightButton
                    onTapped: root.openRowMenu("")
                }
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
                    required property string px4ModeLabel
                    required property string batteryText
                    required property string altitudeText
                    required property string healthWarning
                    required property bool isPx4LinkLost

                    /// 表格顯示大寫短名稱（去掉固定的 _agent 字尾），完整名稱見提示與無障礙名稱
                    readonly property string shortName: agent.replace(/_agent$/, "").toUpperCase()

                    isActive: isSelected
                    implicitHeight: isWaitingForFlightController ? Theme.rowHeight : root.twoLineRowHeight
                    Accessible.name: qsTr("選取 %1").arg(agent)
                    ToolTip.visible: hovered
                    ToolTip.text: healthWarning.length > 0 ? agent + " · " + healthWarning : agent
                    ToolTip.delay: 600
                    onClicked: root.bridge.selectAgent(agent)

                    TapHandler {
                        enabled: root.isVideoAvailable
                        acceptedButtons: Qt.RightButton
                        onTapped: root.openRowMenu(row.agent)
                    }
                    TableRow {
                        anchors.fill: parent
                        cells: row.isWaitingForFlightController
                               ? [row.shortName, qsTr("等待飛控"), "—", "—"]
                               : [row.shortName, row.modeLabel,
                                  row.batteryText === "—" ? "—" : row.batteryText + "%", row.altitudeText]
                        modeDetail: row.isWaitingForFlightController ? "" : row.px4ModeLabel
                        firstColor: Theme.textStrong
                        secondColor: row.isWaitingForFlightController ? Theme.caution : Theme.normal
                        cellColor: row.isWaitingForFlightController ? Theme.textDim : Theme.textValue
                        // PX4 資料中斷（紅）或預檢未通過（琥珀）時在機號旁加警示標記
                        warning: row.healthWarning
                        warningColor: row.isPx4LinkLost ? Theme.danger : Theme.caution
                    }
                }
            }

            // 選取機的姿態、電力與 PX4 健康狀態：固定在表格下方
            Rectangle {
                visible: root.hasSelectedTelemetry || root.hasSelectedHealth
                implicitHeight: 1
                Layout.fillWidth: true
                color: Theme.border
            }
            RowLayout {
                visible: root.hasSelectedTelemetry || root.hasSelectedHealth
                spacing: Theme.spaceLg
                Layout.fillWidth: true
                Layout.margins: Theme.spaceMd
                Layout.leftMargin: Theme.spaceLg

                Item {
                    visible: root.settings.showAttitude && root.hasSelectedTelemetry
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
                    DetailLabel { visible: root.hasSelectedTelemetry; text: "VOLT" }
                    DetailValue {
                        visible: root.hasSelectedTelemetry
                        text: (root.summary.voltageText || "—") + " V"
                    }
                    DetailLabel { visible: root.hasSelectedTelemetry; text: "CURR" }
                    DetailValue {
                        visible: root.hasSelectedTelemetry
                        text: (root.summary.currentText || "—") + " A"
                    }
                    DetailLabel { visible: root.hasSelectedHealth; text: "PX4 LINK" }
                    DetailValue {
                        visible: root.hasSelectedHealth
                        text: root.summary.px4LinkText || "—"
                        color: root.summary.isPx4LinkLost === true ? Theme.danger : Theme.textValue
                    }
                    DetailLabel { visible: root.hasSelectedHealth; text: "PREFLIGHT" }
                    DetailValue {
                        visible: root.hasSelectedHealth
                        text: root.summary.preflightText || "—"
                        color: root.summary.isPreflightFailed === true ? Theme.caution : Theme.textValue
                    }
                }
            }
        }
    }

    // 列的右鍵選單
    Menu {
        id: rowMenu

        /// uav_N_agent；空字串表示「全部」
        property string agent: ""

        padding: 1
        background: Rectangle {
            implicitWidth: 220
            color: Theme.panelDeep
            border.color: Theme.border
        }

        MenuItem {
            id: videoItem

            text: rowMenu.agent.length > 0
                  ? qsTr("設定 %1 影像串流…").arg(rowMenu.agent.replace(/_agent$/, "").toUpperCase())
                  : qsTr("設定機群影像串流…")
            implicitHeight: Theme.rowHeight
            leftPadding: Theme.spaceMd
            rightPadding: Theme.spaceMd
            font.pixelSize: Theme.fontSmall
            onTriggered: root.videoSourceRequested(rowMenu.agent)
            contentItem: RowLayout {
                spacing: Theme.spaceSm

                Icon {
                    name: "video"
                    color: Theme.textMuted
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                }
                Label {
                    text: videoItem.text
                    color: videoItem.highlighted ? Theme.textStrong : Theme.textBody
                    font: videoItem.font
                    Layout.fillWidth: true
                }
            }
            background: Rectangle {
                color: videoItem.highlighted ? Theme.controlHover : "transparent"
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

    /// 四欄表格列：機號、模式、電量、高度（後兩欄靠右）；modeDetail 非空時於模式下方以暗色顯示第二行
    component TableRow: RowLayout {
        id: tableRow

        property var cells: ["", "", "", ""]
        property string modeDetail: ""
        property color cellColor: Theme.textValue
        property color firstColor: cellColor
        property color secondColor: cellColor
        property int cellSize: Theme.fontSmall
        property bool isMono: true
        /// 非空時在機號旁顯示警示標記（內容見列的提示文字）
        property string warning: ""
        property color warningColor: Theme.caution

        spacing: Theme.spaceSm
        Layout.fillWidth: true

        RowLayout {
            spacing: Theme.spaceSm
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spaceLg

            Label {
                text: tableRow.cells[0]
                color: tableRow.firstColor
                font.family: tableRow.isMono ? Theme.fontMono : Theme.fontFamily
                font.pixelSize: tableRow.cellSize
                font.letterSpacing: tableRow.isMono ? 0 : Theme.labelSpacing
                elide: Text.ElideRight
                Layout.fillWidth: tableRow.warning.length === 0
            }
            Rectangle {
                visible: tableRow.warning.length > 0
                implicitWidth: 14
                implicitHeight: 14
                radius: 2
                color: "transparent"
                border.color: tableRow.warningColor
                Accessible.name: tableRow.warning

                Label {
                    anchors.centerIn: parent
                    text: "!"
                    color: tableRow.warningColor
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontLabel
                    font.bold: true
                }
            }
            Item {
                visible: tableRow.warning.length > 0
                Layout.fillWidth: true
            }
        }
        Column {
            id: modeColumn

            readonly property var tableRow: parent

            spacing: 1
            Layout.preferredWidth: root.modeColumnWidth

            Label {
                width: parent.width
                text: modeColumn.tableRow.cells[1]
                color: modeColumn.tableRow.secondColor
                font.family: Theme.fontFamily
                font.pixelSize: modeColumn.tableRow.cellSize
                font.letterSpacing: modeColumn.tableRow.isMono ? 0 : Theme.labelSpacing
                elide: Text.ElideRight
            }
            Label {
                visible: modeColumn.tableRow.modeDetail.length > 0
                width: parent.width
                text: modeColumn.tableRow.modeDetail
                color: Theme.textMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontLabel
                elide: Text.ElideRight
            }
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
