/**
 * @file CommandPanel.qml
 * @brief 飛行控制面板：安全開關／模式控制／移動與 YAW／編隊命令四頁（網頁版 CommandPanel）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 按鈕可用狀態一律取自 UiBridge（與 GuardModule 同一份規則），QML 不自行判斷。
 * L1：Takeoff、RTL、Land、Disarm 與編隊命令需確認對話框；L2：Arm 需滑桿。
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes

Item {
    id: root

    required property var bridge
    required property var settings

    /// 面板是否展開
    property bool isOpen: false
    /// 目前分頁：0 安全開關、1 模式控制、2 移動與 YAW、3 編隊命令
    property int activeTab: 0
    /// 等待 L1 確認的命令
    property string pendingCommand: ""
    property var pendingParams: []
    /// 移動距離（不保存，與網頁版相同預設 3 m）
    property real moveDistance: 3
    /// 展開中的設定表單：takeoff、circle、linear、vert
    property string expandedForm: ""

    readonly property bool isSingle: bridge.isSingleSelected
    readonly property bool isAll: bridge.isAllSelected
    readonly property string modeLabel: isSingle ? (bridge.selectedSummary.modeLabel || qsTr("未知"))
                                                 : (isAll ? qsTr("全體") : qsTr("未知"))
    readonly property string targetLabel: isSingle ? bridge.selectedAgent : qsTr("全體")

    /// L1 命令：需確認後才送出
    readonly property var l1Commands: ["TAKEOFF", "MODE_RTL", "MODE_LAND", "DISARM"]

    function send(command, params) {
        bridge.sendCommand(command, params || [])
    }

    function exec(command, params) {
        if (l1Commands.indexOf(command) >= 0 || command.indexOf("FORMATION_") === 0) {
            pendingCommand = command
            pendingParams = params || []
            confirmDialog.open()
            return
        }
        send(command, params)
    }

    width: launcher.width
    height: launcher.height

    // 開啟按鈕（紙飛機圖示）
    Button {
        id: launcher

        width: 52
        height: 52
        hoverEnabled: true
        Accessible.name: root.isOpen ? qsTr("收合飛行控制") : qsTr("開啟飛行控制")
        ToolTip.visible: hovered
        ToolTip.text: Accessible.name
        onClicked: root.isOpen = !root.isOpen

        background: Rectangle {
            radius: 8
            color: Qt.rgba(17 / 255, 27 / 255, 37 / 255, 0.96)
            border.width: launcher.visualFocus ? 3 : 1
            border.color: root.isOpen || launcher.visualFocus ? Theme.accent : Theme.border
        }
        contentItem: Item {
            Shape {
                anchors.centerIn: parent
                width: 24
                height: 24
                scale: 26 / 24
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeColor: Theme.accent
                    strokeWidth: 1.8
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin
                    PathSvg { path: "M22 2 L15 22 L11 13 L2 9 Z M22 2 L11 13" }
                }
            }
        }
    }

    Rectangle {
        id: panel

        visible: root.isOpen
        y: launcher.height + 10
        width: Math.max(360, Math.min(480, root.parent ? root.parent.width * 0.31 : 400))
        height: Math.min(panelContent.implicitHeight + 32, root.parent ? root.parent.height - 120 : 700)
        radius: 10
        color: Qt.rgba(13 / 255, 22 / 255, 31 / 255, 0.98)
        border.color: Theme.border

        Flickable {
            anchors.fill: parent
            anchors.margins: 16
            contentHeight: panelContent.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            ColumnLayout {
                id: panelContent

                width: parent.width
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: qsTr("飛行控制")
                        color: Theme.textStrong
                        font.pixelSize: Theme.fontTitle
                        Layout.fillWidth: true
                    }
                    ActionButton {
                        iconName: "close"
                        accentColor: "#d7e2ea"
                        implicitWidth: 42
                        implicitHeight: 42
                        Accessible.name: qsTr("收合飛行控制")
                        onClicked: root.isOpen = false
                    }
                }

                Label {
                    visible: !root.isSingle && !root.isAll
                    text: qsTr("請先在機群面板選取無人機")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontBase
                    padding: 14
                }

                Label {
                    visible: root.isSingle || root.isAll
                    text: (root.isSingle ? root.bridge.selectedAgent : qsTr("全部機群")) + " — " + root.modeLabel
                    color: Theme.warning
                    font.pixelSize: Theme.fontBase
                    font.bold: true
                    Layout.bottomMargin: 6
                }

                // 分頁列
                RowLayout {
                    visible: root.isSingle || root.isAll
                    spacing: 6
                    Layout.fillWidth: true

                    Repeater {
                        model: [qsTr("安全開關"), qsTr("模式控制"), qsTr("移動與 YAW"), qsTr("編隊命令")]
                        delegate: Button {
                            id: tabButton

                            required property int index
                            required property string modelData

                            text: modelData
                            Layout.fillWidth: true
                            implicitHeight: 42
                            font.pixelSize: 15
                            onClicked: root.activeTab = index
                            contentItem: Text {
                                text: tabButton.text
                                font: tabButton.font
                                color: root.activeTab === tabButton.index ? Theme.accentSoft : "#b9c9d4"
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }
                            background: Rectangle {
                                radius: 6
                                color: root.activeTab === tabButton.index ? Theme.controlActive : Theme.control
                                border.color: root.activeTab === tabButton.index || tabButton.visualFocus
                                              ? Theme.accent : Theme.border
                            }
                        }
                    }
                }

                StackLayout {
                    id: tabStack

                    visible: root.isSingle || root.isAll
                    currentIndex: root.activeTab
                    Layout.fillWidth: true
                    Layout.minimumHeight: 210
                    // 高度依目前分頁，而非所有分頁中最高者，避免面板下方大片留白
                    Layout.preferredHeight: tabStack.children[root.activeTab]
                                            ? tabStack.children[root.activeTab].implicitHeight : 210
                    Layout.topMargin: 6

                    // 安全開關
                    ColumnLayout {
                        spacing: 8
                        SlideToConfirm {
                            label: qsTr("滑動以解鎖 Arm")
                            accentColor: "#ff4444"
                            enabled: root.bridge.canArm
                            Layout.fillWidth: true
                            onConfirmed: root.send("ARM")
                        }
                        ActionButton {
                            text: "🔒 " + qsTr("Disarm 上鎖")
                            accentColor: Theme.disarm
                            enabled: root.bridge.canDisarm
                            Layout.fillWidth: true
                            onClicked: root.exec("DISARM")
                        }
                        Item { Layout.fillHeight: true }
                    }

                    // 模式控制
                    ColumnLayout {
                        spacing: 8
                        ActionButton {
                            text: "📡 " + qsTr("Offboard 模式")
                            accentColor: Theme.offboard
                            enabled: root.bridge.canOffboard
                            tooltip: qsTr("切換至 Offboard 模式（IDLE 或 OFFBOARD_INIT 時可用）")
                            Layout.fillWidth: true
                            onClicked: root.send("MODE_OFFBOARD")
                        }
                        ActionButton {
                            text: "🚀 " + qsTr("Takeoff 起飛 ") + (root.expandedForm === "takeoff" ? "▲" : "▼")
                            accentColor: Theme.takeoff
                            enabled: root.bridge.canTakeoff
                            Layout.fillWidth: true
                            onClicked: root.expandedForm = root.expandedForm === "takeoff" ? "" : "takeoff"
                        }
                        FormBox {
                            visible: root.expandedForm === "takeoff" && root.bridge.canTakeoff
                            NumberField {
                                id: takeoffField
                                label: qsTr("起飛高度")
                                value: root.settings.takeoffHeight
                                minimum: 0.5
                                maximum: 120
                                tooltip: qsTr("起飛目標高度（公尺），無人機會爬升至指定高度後懸停")
                                onCommitted: (newValue) => root.settings.takeoffHeight = newValue
                            }
                            ActionButton {
                                text: qsTr("確認起飛")
                                accentColor: Theme.takeoff
                                enabled: takeoffField.isDraftValid
                                Layout.fillWidth: true
                                onClicked: {
                                    takeoffField.commit()
                                    root.exec("TAKEOFF", [root.settings.takeoffHeight])
                                    root.expandedForm = ""
                                }
                            }
                        }
                        ActionButton {
                            text: "🏠 " + qsTr("RTL 返航")
                            accentColor: Theme.rtl
                            enabled: root.bridge.canRtl
                            Layout.fillWidth: true
                            onClicked: root.exec("MODE_RTL")
                        }
                        ActionButton {
                            text: "⬇ " + qsTr("Land 降落")
                            accentColor: Theme.land
                            enabled: root.bridge.canLand
                            Layout.fillWidth: true
                            onClicked: root.exec("MODE_LAND")
                        }
                        Item { Layout.fillHeight: true }
                    }

                    // 移動與 YAW
                    ColumnLayout {
                        spacing: 8
                        NumberField {
                            id: moveField
                            label: qsTr("移動距離")
                            value: root.moveDistance
                            minimum: 0
                            tooltip: qsTr("每次移動指令的位移距離（公尺），可輸入任何有限的非負數")
                            onCommitted: (newValue) => root.moveDistance = newValue
                        }
                        GridLayout {
                            columns: 4
                            columnSpacing: 8
                            rowSpacing: 8
                            Layout.fillWidth: true

                            readonly property bool canSendMove: root.bridge.canMove && moveField.isDraftValid

                            Item { Layout.fillWidth: true }
                            MoveButton { text: qsTr("↑ 前"); tooltip: qsTr("前進（北）"); enabled: parent.canSendMove; onClicked: root.exec("MOVE_FORWARD", [root.moveDistance]) }
                            Item { Layout.fillWidth: true }
                            MoveButton { text: qsTr("⬆ 升"); tooltip: qsTr("上升"); accentColor: Theme.takeoff; enabled: parent.canSendMove; onClicked: root.exec("MOVE_UP", [root.moveDistance]) }

                            MoveButton { text: qsTr("← 左"); tooltip: qsTr("左移（西）"); enabled: parent.canSendMove; onClicked: root.exec("MOVE_LEFT", [root.moveDistance]) }
                            Label {
                                text: root.moveDistance + "m"
                                color: Theme.textDim
                                font.pixelSize: Theme.fontBase
                                horizontalAlignment: Text.AlignHCenter
                                Layout.fillWidth: true
                            }
                            MoveButton { text: qsTr("→ 右"); tooltip: qsTr("右移（東）"); enabled: parent.canSendMove; onClicked: root.exec("MOVE_RIGHT", [root.moveDistance]) }
                            MoveButton { text: qsTr("⬇ 降"); tooltip: qsTr("下降"); accentColor: Theme.rtl; enabled: parent.canSendMove; onClicked: root.exec("MOVE_DOWN", [root.moveDistance]) }

                            Item { Layout.fillWidth: true }
                            MoveButton { text: qsTr("↓ 後"); tooltip: qsTr("後退（南）"); enabled: parent.canSendMove; onClicked: root.exec("MOVE_BACKWARD", [root.moveDistance]) }
                            Item { Layout.fillWidth: true }
                            Item { Layout.fillWidth: true }
                        }
                        Rectangle {
                            color: Theme.borderSoft
                            implicitHeight: 1
                            Layout.fillWidth: true
                            Layout.topMargin: 6
                        }
                        NumberField {
                            id: yawField
                            label: qsTr("YAW 旋轉")
                            value: root.settings.yawDegrees
                            minimum: -180
                            maximum: 180
                            unit: "°"
                            tooltip: qsTr("相對旋轉角度；正值順時針，負值逆時針")
                            onCommitted: (newValue) => root.settings.yawDegrees = newValue
                        }
                        ActionButton {
                            text: "⟳ " + qsTr("發送 YAW")
                            accentColor: Theme.yaw
                            enabled: root.bridge.canYaw && yawField.isDraftValid
                            Layout.fillWidth: true
                            onClicked: root.exec("YAW", [root.settings.yawDegrees])
                        }
                        Label {
                            visible: !root.isSingle
                            text: qsTr("YAW 僅支援單機操作")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontBase
                        }
                        Item { Layout.fillHeight: true }
                    }

                    // 編隊命令
                    ColumnLayout {
                        spacing: 8
                        Label {
                            visible: !root.isAll
                            text: qsTr("請選取「全部機群」後使用編隊命令")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontBase
                            padding: 14
                        }
                        ActionButton {
                            visible: root.isAll
                            text: "⭕ " + qsTr("環形編隊 ") + (root.expandedForm === "circle" ? "▲" : "▼")
                            accentColor: Theme.formation
                            enabled: root.bridge.canFormation
                            Layout.fillWidth: true
                            onClicked: root.expandedForm = root.expandedForm === "circle" ? "" : "circle"
                        }
                        FormBox {
                            visible: root.isAll && root.expandedForm === "circle" && root.bridge.canFormation
                            NumberField {
                                id: circleRadius
                                label: qsTr("半徑")
                                value: root.settings.formCircleRadius
                                minimum: 1
                                maximum: 200
                                tooltip: qsTr("環形編隊半徑（公尺），各機圍繞中心點等距排列")
                                onCommitted: (newValue) => root.settings.formCircleRadius = newValue
                            }
                            NumberField {
                                id: circleHeight
                                label: qsTr("高度")
                                value: root.settings.formCircleHeight
                                minimum: 0.5
                                maximum: 120
                                tooltip: qsTr("編隊飛行高度（公尺）")
                                onCommitted: (newValue) => root.settings.formCircleHeight = newValue
                            }
                            ActionButton {
                                text: qsTr("確認發送 環形編隊")
                                accentColor: Theme.formation
                                enabled: circleRadius.isDraftValid && circleHeight.isDraftValid
                                Layout.fillWidth: true
                                onClicked: {
                                    circleRadius.commit()
                                    circleHeight.commit()
                                    root.exec("FORMATION_CIRCLE", [root.settings.formCircleRadius,
                                                                   root.settings.formCircleHeight])
                                    root.expandedForm = ""
                                }
                            }
                        }
                        ActionButton {
                            visible: root.isAll
                            text: "⬌ " + qsTr("水平一字 ") + (root.expandedForm === "linear" ? "▲" : "▼")
                            accentColor: Theme.formation
                            enabled: root.bridge.canFormation
                            Layout.fillWidth: true
                            onClicked: root.expandedForm = root.expandedForm === "linear" ? "" : "linear"
                        }
                        FormBox {
                            visible: root.isAll && root.expandedForm === "linear" && root.bridge.canFormation
                            NumberField {
                                id: linearSpacing
                                label: qsTr("機距")
                                value: root.settings.formLinearSpacing
                                minimum: 1
                                maximum: 200
                                tooltip: qsTr("水平一字編隊機距（公尺），各機之間的水平間隔")
                                onCommitted: (newValue) => root.settings.formLinearSpacing = newValue
                            }
                            NumberField {
                                id: linearHeight
                                label: qsTr("高度")
                                value: root.settings.formLinearHeight
                                minimum: 0.5
                                maximum: 120
                                tooltip: qsTr("編隊飛行高度（公尺）")
                                onCommitted: (newValue) => root.settings.formLinearHeight = newValue
                            }
                            ActionButton {
                                text: qsTr("確認發送 水平一字")
                                accentColor: Theme.formation
                                enabled: linearSpacing.isDraftValid && linearHeight.isDraftValid
                                Layout.fillWidth: true
                                onClicked: {
                                    linearSpacing.commit()
                                    linearHeight.commit()
                                    root.exec("FORMATION_HORI_LINEAR", [root.settings.formLinearSpacing,
                                                                        root.settings.formLinearHeight])
                                    root.expandedForm = ""
                                }
                            }
                        }
                        ActionButton {
                            visible: root.isAll
                            text: "↕ " + qsTr("垂直一字 ") + (root.expandedForm === "vert" ? "▲" : "▼")
                            accentColor: Theme.formation
                            enabled: root.bridge.canFormation
                            Layout.fillWidth: true
                            onClicked: root.expandedForm = root.expandedForm === "vert" ? "" : "vert"
                        }
                        FormBox {
                            visible: root.isAll && root.expandedForm === "vert" && root.bridge.canFormation
                            NumberField {
                                id: vertSpacing
                                label: qsTr("機距")
                                value: root.settings.formVertSpacing
                                minimum: 1
                                maximum: 200
                                tooltip: qsTr("垂直一字編隊機距（公尺），各機之間的垂直間隔")
                                onCommitted: (newValue) => root.settings.formVertSpacing = newValue
                            }
                            ActionButton {
                                text: qsTr("確認發送 垂直一字")
                                accentColor: Theme.formation
                                enabled: vertSpacing.isDraftValid
                                Layout.fillWidth: true
                                onClicked: {
                                    vertSpacing.commit()
                                    root.exec("FORMATION_VERT_LINEAR", [root.settings.formVertSpacing])
                                    root.expandedForm = ""
                                }
                            }
                        }
                        ActionButton {
                            visible: root.isAll
                            text: "✕ " + qsTr("取消編隊")
                            accentColor: Theme.cancel
                            enabled: root.bridge.canFormation
                            Layout.fillWidth: true
                            onClicked: root.exec("FORMATION_CANCEL", [])
                        }
                        Item { Layout.fillHeight: true }
                    }
                }
            }
        }
    }

    ConfirmDialog {
        id: confirmDialog

        title: qsTr("確認執行：") + root.pendingCommand
        message: qsTr("確定要對 %1 發送 %2 命令？").arg(root.targetLabel).arg(root.pendingCommand)
        statusInfo: (root.isSingle ? root.bridge.selectedAgent : qsTr("全體機群")) + " — " + root.modeLabel
        onAccepted: root.send(root.pendingCommand, root.pendingParams)
    }

    component FormBox: Rectangle {
        default property alias content: formColumn.data

        color: Theme.inset
        radius: 8
        border.color: Theme.borderSoft
        implicitHeight: formColumn.implicitHeight + 24
        Layout.fillWidth: true

        ColumnLayout {
            id: formColumn

            anchors.fill: parent
            anchors.margins: 12
            spacing: 8
        }
    }

    component MoveButton: ActionButton {
        accentColor: "#dddddd"
        textAlignment: Text.AlignHCenter
        implicitHeight: 52
        leftPadding: 4
        rightPadding: 4
        Layout.fillWidth: true
        Layout.minimumWidth: 54
    }
}
