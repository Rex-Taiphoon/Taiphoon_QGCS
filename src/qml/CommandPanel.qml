/**
 * @file CommandPanel.qml
 * @brief 飛行控制面板：安全開關／模式控制／移動與 YAW／編隊命令四頁（網頁版 CommandPanel）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 按鈕可用狀態一律取自 UiBridge（與 GuardModule 同一份規則），QML 不自行判斷。
 * L1：Takeoff、RTL、Land、Disarm 與編隊命令需確認對話框；L2：Arm 需滑桿。
 * 按鈕顏色只表達風險：一般（白）、需確認（琥珀）、危險（紅）；面板底部常駐顯示解鎖狀態。
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

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
    /// 解鎖狀態僅在單機且 PX4 狀態有效時可信
    readonly property bool isArmStateKnown: isSingle && bridge.selectedSummary.isPx4Valid === true
    readonly property bool isArmed: isArmStateKnown && bridge.selectedSummary.isArmed === true

    /// L1 命令：需確認後才送出
    readonly property var l1Commands: ["TAKEOFF", "MODE_RTL", "MODE_LAND", "DISARM"]
    /// 需確認命令的附註文字
    readonly property string confirmNote: qsTr("需確認")

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

    /**
     * @brief 可展開表單按鈕的附註：需確認字樣加上展開方向
     * @param form 表單名稱
     * @return 附註文字
     */
    function expandNote(form) {
        return confirmNote + (expandedForm === form ? "  ▴" : "  ▾")
    }

    width: launcher.width
    height: launcher.height

    // 開啟按鈕（紙飛機圖示）
    ActionButton {
        id: launcher

        iconName: "send"
        accentColor: root.isOpen ? Theme.accent : Theme.textBody
        width: 44
        height: 44
        padding: 0
        tooltip: Accessible.name
        Accessible.name: root.isOpen ? qsTr("收合飛行控制") : qsTr("開啟飛行控制")
        onClicked: root.isOpen = !root.isOpen
    }

    Rectangle {
        id: panel

        visible: root.isOpen
        y: launcher.height + Theme.spaceSm
        width: Math.max(340, Math.min(420, root.parent ? root.parent.width * 0.3 : 380))
        height: Math.min(panelContent.implicitHeight + 2, root.parent ? root.parent.height - 120 : 700)
        radius: Theme.radius
        color: Theme.panel
        border.color: Theme.border

        Flickable {
            anchors.fill: parent
            anchors.margins: 1
            contentHeight: panelContent.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            ColumnLayout {
                id: panelContent

                width: parent.width
                spacing: 0

                // 標題列：面板名稱、目標與模式
                RowLayout {
                    spacing: Theme.spaceSm
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.spaceLg
                    Layout.rightMargin: Theme.spaceXs
                    Layout.preferredHeight: 44

                    FieldLabel { text: qsTr("飛行控制") }
                    Label {
                        visible: root.isSingle || root.isAll
                        text: root.targetLabel
                        color: Theme.accent
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontBase
                    }
                    Label {
                        visible: root.isSingle || root.isAll
                        text: root.modeLabel
                        color: Theme.normal
                        font.pixelSize: Theme.fontSmall
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Item { visible: !root.isSingle && !root.isAll; Layout.fillWidth: true }
                    ActionButton {
                        iconName: "close"
                        accentColor: Theme.textMuted
                        implicitWidth: 36
                        implicitHeight: 36
                        padding: 0
                        background: Item {}
                        Accessible.name: qsTr("收合飛行控制")
                        onClicked: root.isOpen = false
                    }
                }
                Divider {}

                Label {
                    visible: !root.isSingle && !root.isAll
                    text: qsTr("請先在機群面板選取無人機")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontBase
                    padding: Theme.spaceLg
                }

                // 分頁列：文字分頁，選取時底線標示
                RowLayout {
                    visible: root.isSingle || root.isAll
                    spacing: 0
                    Layout.fillWidth: true

                    Repeater {
                        model: [qsTr("安全開關"), qsTr("模式控制"), qsTr("移動與 YAW"), qsTr("編隊命令")]
                        delegate: AbstractButton {
                            id: tabButton

                            required property int index
                            required property string modelData

                            readonly property bool isCurrent: root.activeTab === index

                            text: modelData
                            hoverEnabled: true
                            implicitHeight: 38
                            Layout.fillWidth: true
                            Accessible.role: Accessible.PageTab
                            Accessible.name: text
                            onClicked: root.activeTab = index

                            contentItem: Text {
                                text: tabButton.text
                                font.pixelSize: Theme.fontSmall
                                color: tabButton.isCurrent ? Theme.accent
                                                           : (tabButton.hovered ? Theme.textBody : Theme.textMuted)
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }
                            background: Rectangle {
                                color: tabButton.visualFocus ? Theme.controlHover : "transparent"
                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    width: parent.width
                                    height: 2
                                    visible: tabButton.isCurrent
                                    color: Theme.accent
                                }
                            }
                        }
                    }
                }
                Divider { visible: root.isSingle || root.isAll }

                StackLayout {
                    id: tabStack

                    visible: root.isSingle || root.isAll
                    currentIndex: root.activeTab
                    Layout.fillWidth: true
                    Layout.margins: Theme.spaceMd
                    Layout.minimumHeight: 120
                    // 高度依目前分頁，而非所有分頁中最高者，避免面板下方大片留白
                    Layout.preferredHeight: tabStack.children[root.activeTab]
                                            ? tabStack.children[root.activeTab].implicitHeight : 120

                    // 安全開關
                    ColumnLayout {
                        spacing: Theme.spaceSm
                        SlideToConfirm {
                            label: qsTr("滑動以解鎖 Arm")
                            accentColor: Theme.commandDanger
                            enabled: root.bridge.canArm
                            Layout.fillWidth: true
                            onConfirmed: root.send("ARM")
                        }
                        ActionButton {
                            text: qsTr("Disarm 上鎖")
                            leadingIcon: "lock"
                            note: root.confirmNote
                            accentColor: Theme.commandDanger
                            enabled: root.bridge.canDisarm
                            Layout.fillWidth: true
                            onClicked: root.exec("DISARM")
                        }
                        Item { Layout.fillHeight: true }
                    }

                    // 模式控制
                    ColumnLayout {
                        spacing: Theme.spaceSm
                        ActionButton {
                            text: qsTr("Offboard 模式")
                            leadingIcon: "offboard"
                            enabled: root.bridge.canOffboard
                            tooltip: qsTr("切換至 Offboard 模式（IDLE 或 OFFBOARD_INIT 時可用）")
                            Layout.fillWidth: true
                            onClicked: root.send("MODE_OFFBOARD")
                        }
                        ActionButton {
                            text: qsTr("Takeoff 起飛")
                            leadingIcon: "takeoff"
                            note: root.expandNote("takeoff")
                            accentColor: Theme.commandConfirm
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
                                accentColor: Theme.commandConfirm
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
                            text: qsTr("RTL 返航")
                            leadingIcon: "rtl"
                            note: root.confirmNote
                            accentColor: Theme.commandConfirm
                            enabled: root.bridge.canRtl
                            Layout.fillWidth: true
                            onClicked: root.exec("MODE_RTL")
                        }
                        ActionButton {
                            text: qsTr("Land 降落")
                            leadingIcon: "land"
                            note: root.confirmNote
                            accentColor: Theme.commandConfirm
                            enabled: root.bridge.canLand
                            Layout.fillWidth: true
                            onClicked: root.exec("MODE_LAND")
                        }
                        Item { Layout.fillHeight: true }
                    }

                    // 移動與 YAW
                    ColumnLayout {
                        spacing: Theme.spaceSm
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
                            columnSpacing: Theme.spaceSm
                            rowSpacing: Theme.spaceSm
                            Layout.fillWidth: true

                            readonly property bool canSendMove: root.bridge.canMove && moveField.isDraftValid

                            Item { Layout.fillWidth: true }
                            MoveButton { text: qsTr("前"); leadingIcon: "arrowUp"; tooltip: qsTr("沿機頭方向前進"); enabled: parent.canSendMove; onClicked: root.exec("MOVE_FORWARD", [root.moveDistance]) }
                            Item { Layout.fillWidth: true }
                            MoveButton { text: qsTr("升"); leadingIcon: "takeoff"; tooltip: qsTr("上升"); enabled: parent.canSendMove; onClicked: root.exec("MOVE_UP", [root.moveDistance]) }

                            MoveButton { text: qsTr("左"); leadingIcon: "arrowLeft"; tooltip: qsTr("向機頭左側移動"); enabled: parent.canSendMove; onClicked: root.exec("MOVE_LEFT", [root.moveDistance]) }
                            Label {
                                text: root.moveDistance + " m"
                                color: Theme.textMuted
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.fontBase
                                horizontalAlignment: Text.AlignHCenter
                                Layout.fillWidth: true
                            }
                            MoveButton { text: qsTr("右"); leadingIcon: "arrowRight"; tooltip: qsTr("向機頭右側移動"); enabled: parent.canSendMove; onClicked: root.exec("MOVE_RIGHT", [root.moveDistance]) }
                            MoveButton { text: qsTr("降"); leadingIcon: "land"; tooltip: qsTr("下降"); enabled: parent.canSendMove; onClicked: root.exec("MOVE_DOWN", [root.moveDistance]) }

                            Item { Layout.fillWidth: true }
                            MoveButton { text: qsTr("後"); leadingIcon: "arrowDown"; tooltip: qsTr("沿機頭方向後退"); enabled: parent.canSendMove; onClicked: root.exec("MOVE_BACKWARD", [root.moveDistance]) }
                            Item { Layout.fillWidth: true }
                            Item { Layout.fillWidth: true }
                        }
                        Label {
                            // Agent 單機水平移動依保存的 target yaw，前後左右相對機頭而非東南西北
                            text: qsTr("前後左右以機頭方向為準")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontLabel
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                        }
                        Divider { Layout.topMargin: Theme.spaceXs }
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
                            text: qsTr("發送 YAW")
                            leadingIcon: "yaw"
                            enabled: root.bridge.canYaw && yawField.isDraftValid
                            Layout.fillWidth: true
                            onClicked: root.exec("YAW", [root.settings.yawDegrees])
                        }
                        Label {
                            visible: !root.isSingle
                            text: qsTr("YAW 僅支援單機操作")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSmall
                        }
                        Item { Layout.fillHeight: true }
                    }

                    // 編隊命令
                    ColumnLayout {
                        spacing: Theme.spaceSm
                        Label {
                            visible: !root.isAll
                            text: qsTr("請選取「全部機群」後使用編隊命令")
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontBase
                            padding: Theme.spaceXs
                        }
                        ActionButton {
                            visible: root.isAll
                            text: qsTr("環形編隊")
                            leadingIcon: "formCircle"
                            note: root.expandNote("circle")
                            accentColor: Theme.commandConfirm
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
                                accentColor: Theme.commandConfirm
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
                            text: qsTr("水平一字")
                            leadingIcon: "formLine"
                            note: root.expandNote("linear")
                            accentColor: Theme.commandConfirm
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
                                accentColor: Theme.commandConfirm
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
                            text: qsTr("垂直一字")
                            leadingIcon: "formVertical"
                            note: root.expandNote("vert")
                            accentColor: Theme.commandConfirm
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
                                accentColor: Theme.commandConfirm
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
                            text: qsTr("取消編隊")
                            leadingIcon: "close"
                            note: root.confirmNote
                            accentColor: Theme.commandConfirm
                            enabled: root.bridge.canFormation
                            Layout.fillWidth: true
                            onClicked: root.exec("FORMATION_CANCEL", [])
                        }
                        Item { Layout.fillHeight: true }
                    }
                }

                // 安全狀態常駐列：不論目前在哪個分頁都看得到是否已解鎖
                Divider { visible: root.isSingle || root.isAll }
                RowLayout {
                    visible: root.isSingle || root.isAll
                    spacing: Theme.spaceMd
                    Layout.fillWidth: true
                    Layout.leftMargin: Theme.spaceLg
                    Layout.rightMargin: Theme.spaceLg
                    Layout.preferredHeight: 44

                    FieldLabel { text: qsTr("安全狀態") }
                    Item { Layout.fillWidth: true }
                    Label {
                        text: !root.isSingle ? qsTr("多機，請個別確認")
                                             : (!root.isArmStateKnown ? qsTr("未確認")
                                                                      : (root.isArmed ? qsTr("已解鎖") : qsTr("已上鎖")))
                        color: !root.isArmStateKnown ? Theme.textMuted : (root.isArmed ? Theme.danger : Theme.normal)
                        font.pixelSize: Theme.fontSmall
                        leftPadding: root.isArmStateKnown ? Theme.spaceSm : 0
                        rightPadding: leftPadding
                        topPadding: root.isArmStateKnown ? 2 : 0
                        bottomPadding: topPadding
                        background: Rectangle {
                            visible: root.isArmStateKnown
                            color: "transparent"
                            border.color: root.isArmed ? Theme.danger : Theme.normal
                        }
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

    component FieldLabel: Label {
        color: Theme.textMuted
        font.pixelSize: Theme.fontLabel
        font.letterSpacing: Theme.labelSpacing
    }

    component Divider: Rectangle {
        color: Theme.border
        implicitHeight: 1
        Layout.fillWidth: true
    }

    component FormBox: Rectangle {
        default property alias content: formColumn.data

        color: Theme.inset
        radius: Theme.radius
        border.color: Theme.borderSoft
        implicitHeight: formColumn.implicitHeight + 2 * Theme.spaceMd
        Layout.fillWidth: true

        ColumnLayout {
            id: formColumn

            anchors.fill: parent
            anchors.margins: Theme.spaceMd
            spacing: Theme.spaceSm
        }
    }

    component MoveButton: ActionButton {
        textAlignment: Text.AlignHCenter
        implicitHeight: 44
        leftPadding: Theme.spaceXs
        rightPadding: Theme.spaceXs
        Layout.fillWidth: true
        Layout.minimumWidth: 54
    }
}
