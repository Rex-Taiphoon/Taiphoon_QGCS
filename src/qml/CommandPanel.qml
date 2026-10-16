/**
 * @file CommandPanel.qml
 * @brief 飛行控制面板（MCDU 列式）：SAFETY／MODE／MOVE／FORM 四頁，L1 命令經面板內確認列 EXEC 後送出
 * @author Taiphoon GCS Team
 * @date 2026-10-15
 *
 * 每列左側為行選鍵「‹」，右側小字為狀態：可用時標示 CONFIRM（需確認），停用時寫出 Guard 規則摘要。
 * 命令可用狀態一律取自 UiBridge（與 GuardModule 同一份規則），右側停用原因只是說明文字，不參與判斷。
 * L1：Takeoff、RTL、Land、Disarm 與編隊命令先暫存到確認列（顯示「是否執行？」），按 EXEC 才送出；
 *     確認列在選取目標改變、命令不再可用或逾時後自動清除，EXEC 時再核對目標與可用狀態。
 *     編隊點選隊形後展開參數，同一列右側出現閃爍的 CONFIRM，按下即暫存到確認列。
 * L2：Arm 需滑桿。面板底部常駐顯示解鎖狀態與 DISARM。
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    required property var bridge
    required property var settings

    /// 面板是否展開；收合時只保留標題列
    property bool isOpen: true
    /// 目前分頁：0 SAFETY、1 MODE、2 MOVE、3 FORM
    property int activeTab: 0
    /// 確認列暫存的 L1 命令：{ command, params, label, target }；null 表示沒有待確認命令
    property var pending: null
    /// 移動距離（不保存，與網頁版相同預設 3 m）
    property real moveDistance: 3
    /// 展開中的編隊參數：circle、linear、vshape
    property string expandedForm: ""

    readonly property bool isSingle: bridge.isSingleSelected
    readonly property bool isAll: bridge.isAllSelected
    readonly property bool hasTarget: isSingle || isAll
    readonly property var summary: bridge.selectedSummary
    readonly property bool isWaitingForFlightController: isSingle && summary.isWaitingForFlightController === true
    readonly property string modeLabel: isSingle ? (summary.modeLabel || qsTr("未知"))
                                                 : (isAll ? qsTr("全體") : "")
    readonly property string targetLabel: isSingle ? bridge.selectedAgent.replace(/_agent$/, "").toUpperCase()
                                                   : (isAll ? qsTr("全部機群") : "")
    /// 解鎖狀態僅在單機且 PX4 狀態有效時可信
    readonly property bool isArmStateKnown: isSingle && summary.isPx4Valid === true
    readonly property bool isArmed: isArmStateKnown && summary.isArmed === true
    /// 確認列的命令目前是否仍可送出（目標未變且 UiBridge 仍允許）
    readonly property bool isPendingSendable: pending !== null && pending.target === bridge.selectedAgent
                                              && isAllowed(pending.command)
    /// 確認列逾時（毫秒）
    readonly property int pendingTimeoutMs: 15000
    readonly property int headerHeight: 40

    /**
     * @brief 查詢 L1 命令目前是否可用（取自 UiBridge，QML 不自行判斷）
     * @param command 命令名稱
     * @return 是否可用
     */
    function isAllowed(command) {
        switch (command) {
        case "TAKEOFF": return bridge.canTakeoff
        case "MODE_RTL": return bridge.canRtl
        case "MODE_LAND": return bridge.canLand
        case "DISARM": return bridge.canDisarm
        default: return command.indexOf("FORMATION_") === 0 && bridge.canFormation
        }
    }

    /**
     * @brief 立即送出不需確認的命令
     * @param command 命令名稱
     * @param params 參數
     */
    function send(command, params) {
        bridge.sendCommand(command, params || [])
    }

    /**
     * @brief 將 L1 命令暫存到確認列，等待 EXEC
     * @param command 命令名稱
     * @param params 參數
     * @param label 確認列顯示的命令文字
     */
    function stage(command, params, label) {
        pending = { command: command, params: params || [], label: label, target: bridge.selectedAgent }
        pendingTimer.restart()
    }

    function clearPending() {
        pending = null
        pendingTimer.stop()
    }

    /**
     * @brief EXEC：再次核對目標與可用狀態後送出確認列的命令
     */
    function executePending() {
        if (!isPendingSendable) {
            clearPending()
            return
        }
        const command = pending.command
        const params = pending.params
        clearPending()
        send(command, params)
    }

    /**
     * @brief 列右側狀態：可用時標示需確認，停用時顯示原因
     * @param isEnabled 命令是否可用
     * @param needsConfirm 是否為 L1 命令
     * @param reason 停用原因（Guard 規則摘要）
     * @return 狀態文字
     */
    function rowTag(isEnabled, needsConfirm, reason) {
        if (isEnabled) {
            return needsConfirm ? "CONFIRM" : ""
        }
        return isWaitingForFlightController ? qsTr("等待飛控") : reason
    }

    width: 344
    height: isOpen ? Math.min(panelContent.implicitHeight + 2, parent ? parent.height - 2 * Theme.spaceLg : 700)
                   : headerHeight
    color: Theme.panel
    radius: Theme.radius
    border.color: Theme.border
    clip: true

    onActiveTabChanged: expandedForm = ""

    // 確認列逾時清除，避免久置的命令在狀況改變後被誤送
    Timer {
        id: pendingTimer

        interval: root.pendingTimeoutMs
        onTriggered: root.clearPending()
    }

    Connections {
        target: root.bridge
        function onSelectionChanged() {
            root.clearPending()
            root.expandedForm = ""
        }
        function onAvailabilityChanged() {
            if (root.pending !== null && !root.isAllowed(root.pending.command)) {
                root.clearPending()
            }
        }
    }

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

            // 標題列：面板名稱、目標、模式與收合
            RowLayout {
                spacing: Theme.spaceSm
                Layout.fillWidth: true
                Layout.leftMargin: Theme.spaceLg
                Layout.rightMargin: Theme.spaceXs
                Layout.preferredHeight: root.headerHeight - 1

                Label {
                    text: "FLT CTRL"
                    color: Theme.textBody
                    font.pixelSize: Theme.fontLabel + 1
                    font.letterSpacing: Theme.labelSpacing
                }
                Item { Layout.fillWidth: true }
                Label {
                    visible: root.isSingle
                    text: root.modeLabel
                    color: root.isWaitingForFlightController ? Theme.caution : Theme.normal
                    font.pixelSize: Theme.fontSmall
                    elide: Text.ElideRight
                    Layout.maximumWidth: 120
                }
                Label {
                    visible: root.hasTarget
                    text: root.targetLabel
                    color: Theme.accent
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontBase
                }
                ActionButton {
                    iconName: root.isOpen ? "minus" : "chevronDown"
                    accentColor: Theme.textMuted
                    implicitWidth: 32
                    implicitHeight: 32
                    padding: 0
                    background: Item {}
                    tooltip: root.isOpen ? qsTr("收合飛行控制") : qsTr("展開飛行控制")
                    Accessible.name: tooltip
                    onClicked: root.isOpen = !root.isOpen
                }
            }
            Divider {}

            Label {
                visible: !root.hasTarget
                text: qsTr("請先在機群面板選取無人機")
                color: Theme.textMuted
                font.pixelSize: Theme.fontSmall
                padding: Theme.spaceLg
            }

            // 分頁列：英文縮寫，選取時青色並加底線
            RowLayout {
                visible: root.hasTarget
                spacing: 0
                Layout.fillWidth: true

                Repeater {
                    model: ["SAFETY", "MODE", "MOVE", "FORM"]
                    delegate: AbstractButton {
                        id: tabButton

                        required property int index
                        required property string modelData

                        readonly property bool isCurrent: root.activeTab === index

                        text: modelData
                        hoverEnabled: true
                        implicitHeight: 34
                        Layout.fillWidth: true
                        Accessible.role: Accessible.PageTab
                        Accessible.name: text
                        onClicked: root.activeTab = index

                        contentItem: Text {
                            text: tabButton.text
                            font.pixelSize: Theme.fontLabel
                            font.letterSpacing: Theme.labelSpacing
                            color: tabButton.isCurrent ? Theme.accent
                                                       : (tabButton.hovered ? Theme.textBody : Theme.textMuted)
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
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
            Divider { visible: root.hasTarget }

            StackLayout {
                id: tabStack

                visible: root.hasTarget
                currentIndex: root.activeTab
                Layout.fillWidth: true
                // 高度依目前分頁，而非所有分頁中最高者，避免面板下方大片留白
                Layout.preferredHeight: tabStack.children[root.activeTab]
                                        ? tabStack.children[root.activeTab].implicitHeight : 0

                // SAFETY
                ColumnLayout {
                    spacing: 0

                    SlideToConfirm {
                        label: qsTr("滑動以解鎖 ARM")
                        accentColor: Theme.commandDanger
                        enabled: root.bridge.canArm
                        implicitHeight: 46
                        Layout.fillWidth: true
                        Layout.margins: Theme.spaceMd
                        onConfirmed: root.send("ARM")
                    }
                    Label {
                        visible: !root.bridge.canArm
                        text: root.isWaitingForFlightController ? qsTr("等待飛控")
                                                                : qsTr("需 PX4 為 Offboard、已落地且上鎖")
                        color: Theme.textDim
                        font.pixelSize: Theme.fontLabel
                        Layout.leftMargin: Theme.spaceLg
                        Layout.bottomMargin: Theme.spaceSm
                    }
                    Divider {}
                    LskRow {
                        title: qsTr("DISARM 上鎖")
                        risk: Theme.commandDanger
                        enabled: root.bridge.canDisarm
                        tag: root.rowTag(enabled, true, qsTr("限解鎖／懸停"))
                        onClicked: root.stage("DISARM", [], title)
                    }
                }

                // MODE
                ColumnLayout {
                    spacing: 0

                    LskRow {
                        title: qsTr("OFFBOARD 模式")
                        enabled: root.bridge.canOffboard
                        tag: root.rowTag(enabled, false, qsTr("限閒置"))
                        tooltip: qsTr("切換至 Offboard 模式（IDLE 或 OFFBOARD_INIT 時可用）")
                        onClicked: root.send("MODE_OFFBOARD")
                    }
                    LskRow {
                        title: qsTr("TAKEOFF 起飛")
                        risk: Theme.commandConfirm
                        // 可用狀態不綁輸入合法性：否則清空欄位重打時整列（含欄位）會被停用
                        enabled: root.bridge.canTakeoff
                        tag: root.rowTag(enabled, true, qsTr("需解鎖並落地"))
                        onClicked: {
                            if (!takeoffField.isDraftValid) {
                                return
                            }
                            takeoffField.commit()
                            root.stage("TAKEOFF", [root.settings.takeoffHeight],
                                       title + " " + root.settings.takeoffHeight + " M")
                        }

                        InlineNumber {
                            id: takeoffField

                            value: root.settings.takeoffHeight
                            minimum: 0.5
                            maximum: 120
                            unit: "M"
                            isActive: root.bridge.canTakeoff
                            tooltip: qsTr("起飛目標高度（公尺），無人機會爬升至指定高度後懸停")
                            onCommitted: (newValue) => root.settings.takeoffHeight = newValue
                        }
                    }
                    LskRow {
                        title: qsTr("RTL 返航")
                        risk: Theme.commandConfirm
                        enabled: root.bridge.canRtl
                        tag: root.rowTag(enabled, true, qsTr("限懸停／移動"))
                        onClicked: root.stage("MODE_RTL", [], title)
                    }
                    LskRow {
                        title: qsTr("LAND 降落")
                        risk: Theme.commandConfirm
                        enabled: root.bridge.canLand
                        tag: root.rowTag(enabled, true, qsTr("限懸停／移動"))
                        onClicked: root.stage("MODE_LAND", [], title)
                    }
                }

                // MOVE
                ColumnLayout {
                    spacing: 0

                    RowLayout {
                        spacing: Theme.spaceSm
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.spaceLg
                        Layout.rightMargin: Theme.spaceLg
                        Layout.preferredHeight: 40

                        FieldLabel { text: "STEP" }
                        Item { Layout.fillWidth: true }
                        InlineNumber {
                            id: moveField

                            value: root.moveDistance
                            minimum: 0
                            unit: "M"
                            isActive: root.bridge.canMove
                            tooltip: qsTr("每次移動指令的位移距離（公尺），可輸入任何有限的非負數")
                            onCommitted: (newValue) => root.moveDistance = newValue
                        }
                    }
                    Divider {}

                    // 方向鍵：前後左右以機頭方向為準，右側為升降
                    GridLayout {
                        id: movePad

                        readonly property bool canSendMove: root.bridge.canMove && moveField.isDraftValid

                        columns: 4
                        columnSpacing: Theme.spaceXs
                        rowSpacing: Theme.spaceXs
                        Layout.fillWidth: true
                        Layout.margins: Theme.spaceMd

                        Item { Layout.fillWidth: true }
                        MoveButton {
                            iconName: "arrowUp"
                            tooltip: qsTr("沿機頭方向前進")
                            enabled: movePad.canSendMove
                            onClicked: root.send("MOVE_FORWARD", [root.moveDistance])
                        }
                        Item { Layout.fillWidth: true }
                        MoveButton {
                            iconName: "takeoff"
                            tooltip: qsTr("上升")
                            enabled: movePad.canSendMove
                            onClicked: root.send("MOVE_UP", [root.moveDistance])
                        }

                        MoveButton {

                            iconName: "arrowLeft"

                            tooltip: qsTr("向機頭左側移動")

                            enabled: movePad.canSendMove

                            onClicked: root.send("MOVE_LEFT", [root.moveDistance])

                        }
                        Label {
                            text: "HDG"
                            color: Theme.textDim
                            font.pixelSize: Theme.fontLabel
                            font.letterSpacing: Theme.labelSpacing
                            horizontalAlignment: Text.AlignHCenter
                            Layout.fillWidth: true
                        }
                        MoveButton {
                            iconName: "arrowRight"
                            tooltip: qsTr("向機頭右側移動")
                            enabled: movePad.canSendMove
                            onClicked: root.send("MOVE_RIGHT", [root.moveDistance])
                        }
                        MoveButton {
                            iconName: "land"
                            tooltip: qsTr("下降")
                            enabled: movePad.canSendMove
                            onClicked: root.send("MOVE_DOWN", [root.moveDistance])
                        }

                        Item { Layout.fillWidth: true }
                        MoveButton {
                            iconName: "arrowDown"
                            tooltip: qsTr("沿機頭方向後退")
                            enabled: movePad.canSendMove
                            onClicked: root.send("MOVE_BACKWARD", [root.moveDistance])
                        }
                        Item { Layout.fillWidth: true }
                        Item { Layout.fillWidth: true }
                    }
                    Label {
                        // Agent 單機水平移動依保存的 target yaw，前後左右相對機頭而非東南西北
                        text: root.bridge.canMove ? qsTr("前後左右以機頭方向為準")
                                                  : root.rowTag(false, false, qsTr("限起飛／懸停／移動"))
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontLabel
                        horizontalAlignment: Text.AlignHCenter
                        Layout.fillWidth: true
                        Layout.bottomMargin: Theme.spaceSm
                    }
                    Divider {}
                    LskRow {
                        title: qsTr("YAW 旋轉")
                        enabled: root.bridge.canYaw
                        tag: root.rowTag(enabled, false, root.isSingle ? qsTr("限懸停／移動") : qsTr("限單機"))
                        onClicked: {
                            if (!yawField.isDraftValid) {
                                return
                            }
                            yawField.commit()
                            root.send("YAW", [root.settings.yawDegrees])
                        }

                        InlineNumber {
                            id: yawField

                            value: root.settings.yawDegrees
                            minimum: -180
                            maximum: 180
                            unit: "°"
                            isActive: root.bridge.canYaw
                            tooltip: qsTr("相對旋轉角度；正值順時針，負值逆時針")
                            onCommitted: (newValue) => root.settings.yawDegrees = newValue
                        }
                    }
                }

                // FORM
                ColumnLayout {
                    spacing: 0

                    Label {
                        visible: !root.isAll
                        text: qsTr("請選取「全部機群」後使用編隊命令")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSmall
                        padding: Theme.spaceLg
                    }

                    LskRow {
                        visible: root.isAll
                        title: qsTr("環形編隊")
                        risk: Theme.commandConfirm
                        enabled: root.bridge.canFormation
                        isExpanded: root.expandedForm === "circle"
                        tag: enabled ? "▾" : qsTr("需 2 台以上")
                        actionText: isExpanded ? "CONFIRM" : ""
                        isActionEnabled: circleRadius.isDraftValid && circleHeight.isDraftValid
                        onClicked: root.expandedForm = isExpanded ? "" : "circle"
                        onActionClicked: {
                            circleRadius.commit()
                            circleHeight.commit()
                            root.stage("FORMATION_CIRCLE",
                                       [root.settings.formCircleRadius, root.settings.formCircleHeight],
                                       qsTr("環形編隊 R%1 H%2").arg(root.settings.formCircleRadius)
                                                                .arg(root.settings.formCircleHeight))
                        }
                    }
                    FormBox {
                        visible: root.isAll && root.expandedForm === "circle" && root.bridge.canFormation

                        FormField {
                            label: "RADIUS"
                            InlineNumber {
                                id: circleRadius
                                value: root.settings.formCircleRadius
                                minimum: 1
                                maximum: 200
                                unit: "M"
                                tooltip: qsTr("環形編隊半徑（公尺），各機圍繞中心點等距排列")
                                onCommitted: (newValue) => root.settings.formCircleRadius = newValue
                            }
                        }
                        FormField {
                            label: "ALT"
                            InlineNumber {
                                id: circleHeight
                                value: root.settings.formCircleHeight
                                minimum: 0.5
                                maximum: 120
                                unit: "M"
                                tooltip: qsTr("編隊飛行高度（公尺）")
                                onCommitted: (newValue) => root.settings.formCircleHeight = newValue
                            }
                        }
                    }

                    LskRow {
                        visible: root.isAll
                        title: qsTr("水平一字")
                        risk: Theme.commandConfirm
                        enabled: root.bridge.canFormation
                        isExpanded: root.expandedForm === "linear"
                        tag: enabled ? "▾" : qsTr("需 2 台以上")
                        actionText: isExpanded ? "CONFIRM" : ""
                        isActionEnabled: linearSpacing.isDraftValid && linearHeight.isDraftValid
                        onClicked: root.expandedForm = isExpanded ? "" : "linear"
                        onActionClicked: {
                            linearSpacing.commit()
                            linearHeight.commit()
                            root.stage("FORMATION_HORI_LINEAR",
                                       [root.settings.formLinearSpacing, root.settings.formLinearHeight],
                                       qsTr("水平一字 S%1 H%2").arg(root.settings.formLinearSpacing)
                                                               .arg(root.settings.formLinearHeight))
                        }
                    }
                    FormBox {
                        visible: root.isAll && root.expandedForm === "linear" && root.bridge.canFormation

                        FormField {
                            label: "SPACING"
                            InlineNumber {
                                id: linearSpacing
                                value: root.settings.formLinearSpacing
                                minimum: 1
                                maximum: 200
                                unit: "M"
                                tooltip: qsTr("水平一字編隊機距（公尺），各機之間的水平間隔")
                                onCommitted: (newValue) => root.settings.formLinearSpacing = newValue
                            }
                        }
                        FormField {
                            label: "ALT"
                            InlineNumber {
                                id: linearHeight
                                value: root.settings.formLinearHeight
                                minimum: 0.5
                                maximum: 120
                                unit: "M"
                                tooltip: qsTr("編隊飛行高度（公尺）")
                                onCommitted: (newValue) => root.settings.formLinearHeight = newValue
                            }
                        }
                    }

                    // 垂直一字暫不開放（Agent 仍支援 FORMATION_VERT_LINEAR，待驗證後再加回列表）
                    LskRow {
                        visible: root.isAll
                        title: qsTr("燕行編隊")
                        risk: Theme.commandConfirm
                        enabled: root.bridge.canFormation
                        isExpanded: root.expandedForm === "vshape"
                        tag: enabled ? "▾" : qsTr("需 2 台以上")
                        actionText: isExpanded ? "CONFIRM" : ""
                        isActionEnabled: vShapeSpacing.isDraftValid && vShapeHeight.isDraftValid
                        onClicked: root.expandedForm = isExpanded ? "" : "vshape"
                        onActionClicked: {
                            vShapeSpacing.commit()
                            vShapeHeight.commit()
                            root.stage("FORMATION_V_SHAPE",
                                       [root.settings.formVShapeSpacing, root.settings.formVShapeHeight],
                                       qsTr("燕行編隊 S%1 H%2").arg(root.settings.formVShapeSpacing)
                                                               .arg(root.settings.formVShapeHeight))
                        }
                    }
                    FormBox {
                        visible: root.isAll && root.expandedForm === "vshape" && root.bridge.canFormation

                        FormField {
                            label: "SPACING"
                            InlineNumber {
                                id: vShapeSpacing
                                value: root.settings.formVShapeSpacing
                                minimum: 2
                                maximum: 200
                                unit: "M"
                                tooltip: qsTr("燕行（V 字）編隊同一翼相鄰兩機距離（公尺），頂點朝北")
                                onCommitted: (newValue) => root.settings.formVShapeSpacing = newValue
                            }
                        }
                        FormField {
                            label: "ALT"
                            InlineNumber {
                                id: vShapeHeight
                                value: root.settings.formVShapeHeight
                                minimum: 0.5
                                maximum: 120
                                unit: "M"
                                tooltip: qsTr("編隊飛行高度（公尺）")
                                onCommitted: (newValue) => root.settings.formVShapeHeight = newValue
                            }
                        }
                    }

                    LskRow {
                        visible: root.isAll
                        title: qsTr("取消編隊")
                        risk: Theme.commandConfirm
                        enabled: root.bridge.canFormation
                        tag: root.rowTag(enabled, true, qsTr("需 2 台以上"))
                        onClicked: root.stage("FORMATION_CANCEL", [], title)
                    }
                }
            }

            // 確認列（MCDU scratchpad）：L1 命令暫存於此，按 EXEC 才送出
            Rectangle {
                visible: root.hasTarget && root.pending !== null
                color: Theme.cautionInset
                implicitHeight: 44
                Layout.fillWidth: true

                Rectangle { width: parent.width; height: 1; color: Qt.alpha(Theme.caution, 0.35) }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.spaceLg
                    anchors.rightMargin: Theme.spaceMd
                    spacing: Theme.spaceSm

                    Label {
                        text: qsTr("是否執行？")
                        color: Theme.caution
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontSmall
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    ScratchButton {
                        text: "CLR"
                        accentColor: Theme.textMuted
                        tooltip: qsTr("取消這個命令")
                        onClicked: root.clearPending()
                    }
                    // EXEC 以實心琥珀色高亮，與 CLR 明顯區隔
                    ScratchButton {
                        text: "EXEC"
                        accentColor: Theme.background
                        isFilled: true
                        fillColor: Theme.caution
                        enabled: root.isPendingSendable
                        tooltip: qsTr("送出確認列的命令")
                        Accessible.name: root.pending === null ? text : qsTr("執行 %1").arg(root.pending.label)
                        onClicked: root.executePending()
                    }
                }
            }

            // 安全狀態常駐列：不論目前在哪個分頁都看得到是否已解鎖，並可直接上鎖
            Divider { visible: root.hasTarget }
            RowLayout {
                visible: root.hasTarget
                spacing: Theme.spaceMd
                Layout.fillWidth: true
                Layout.leftMargin: Theme.spaceLg
                Layout.rightMargin: Theme.spaceMd
                Layout.preferredHeight: 44

                FieldLabel { text: "SAFETY" }
                Label {
                    text: !root.isSingle ? qsTr("多機，請個別確認")
                                         : (!root.isArmStateKnown ? qsTr("未確認")
                                                                  : (root.isArmed ? qsTr("已解鎖") : qsTr("已上鎖")))
                    color: !root.isArmStateKnown ? Theme.textMuted : (root.isArmed ? Theme.danger : Theme.normal)
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSmall
                    leftPadding: root.isArmStateKnown ? Theme.spaceSm : 0
                    rightPadding: leftPadding
                    topPadding: root.isArmStateKnown ? 1 : 0
                    bottomPadding: topPadding
                    background: Rectangle {
                        visible: root.isArmStateKnown
                        color: "transparent"
                        border.color: root.isArmed ? Theme.danger : Theme.normal
                    }
                }
                Item { Layout.fillWidth: true }
                ScratchButton {
                    visible: root.isSingle
                    text: "DISARM"
                    accentColor: Theme.danger
                    enabled: root.bridge.canDisarm
                    tooltip: qsTr("上鎖（需在確認列按 EXEC）")
                    onClicked: root.stage("DISARM", [], qsTr("DISARM 上鎖"))
                }
            }
        }
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

    /// MCDU 命令列：左側行選鍵、命令名稱（依風險上色）、可選的行內欄位、右側狀態小字
    component LskRow: AbstractButton {
        id: lsk

        /// 命令名稱
        property string title: ""
        /// 風險色：一般（白）、需確認（琥珀）、危險（紅）
        property color risk: Theme.commandNormal
        /// 右側狀態：CONFIRM 或停用原因
        property string tag: ""
        property string tooltip: ""
        /// 行內欄位（例如起飛高度），放在命令名稱之後
        default property alias fields: fieldSlot.data
        /// 展開參數區時以內嵌底色與下方參數區連成同一框
        property bool isExpanded: false
        /// 右側動作按鈕文字（例如 CONFIRM）；非空時取代狀態小字並閃爍提示
        property string actionText: ""
        property bool isActionEnabled: true

        readonly property color titleColor: enabled ? risk : Theme.textDim

        /// 按下右側動作按鈕
        signal actionClicked()

        implicitHeight: 40
        Layout.fillWidth: true
        hoverEnabled: true
        Accessible.role: Accessible.Button
        Accessible.name: title + (tag.length > 0 ? "，" + tag : "")
        ToolTip.visible: tooltip.length > 0 && hovered
        ToolTip.text: tooltip
        ToolTip.delay: 600

        contentItem: RowLayout {
            spacing: Theme.spaceSm

            Text {
                text: "‹"
                color: lsk.enabled ? Theme.accent : Theme.textDim
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontBase + 2
                Layout.preferredWidth: 14
            }
            Text {
                text: lsk.title
                color: lsk.titleColor
                font.pixelSize: Theme.fontBase - 1
                elide: Text.ElideRight
            }
            RowLayout {
                id: fieldSlot

                spacing: Theme.spaceXs
            }
            Item { Layout.fillWidth: true }
            Text {
                visible: lsk.tag.length > 0 && lsk.actionText.length === 0
                text: lsk.tag
                color: !lsk.enabled ? Theme.textDim
                                    : (lsk.risk === Theme.commandNormal ? Theme.textMuted : lsk.risk)
                font.pixelSize: Theme.fontLabel
                font.letterSpacing: lsk.tag === "CONFIRM" ? Theme.labelSpacing : 0
            }
            ScratchButton {
                id: actionButton

                visible: lsk.actionText.length > 0
                text: lsk.actionText
                accentColor: Theme.caution
                enabled: lsk.isActionEnabled
                Accessible.name: qsTr("確認%1").arg(lsk.title)
                onClicked: lsk.actionClicked()

                // 尚未暫存命令時閃爍，提示需按 CONFIRM；停止時回到完全不透明
                SequentialAnimation on opacity {
                    running: actionButton.visible && actionButton.enabled && root.pending === null
                    loops: Animation.Infinite
                    alwaysRunToEnd: true
                    NumberAnimation { to: 0.3; duration: 500; easing.type: Easing.InOutQuad }
                    NumberAnimation { to: 1.0; duration: 500; easing.type: Easing.InOutQuad }
                }
            }
        }
        leftPadding: Theme.spaceLg
        rightPadding: Theme.spaceLg

        background: Rectangle {
            color: lsk.down ? Theme.controlActive
                            : ((lsk.hovered && lsk.enabled) || lsk.visualFocus
                               ? Theme.controlHover : (lsk.isExpanded ? Theme.inset : "transparent"))
            border.width: lsk.visualFocus ? 1 : 0
            border.color: Theme.accent

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.borderSoft
            }
        }
    }

    /// 行內數字欄位：青色等寬字加虛線底線（MCDU 可編輯欄位），失焦時不合法則還原
    component InlineNumber: RowLayout {
        id: inline

        property real value: 0
        property real minimum: 0.1
        /// NaN 表示不限
        property real maximum: NaN
        property string unit: "M"
        property string tooltip: ""
        /// 所屬命令可用時以青色顯示，否則變暗
        property bool isActive: true
        readonly property bool isDraftValid: parseDraft(input.text) !== null

        signal committed(real newValue)

        /**
         * @brief 解析輸入文字
         * @param raw 輸入
         * @return 合法數值或 null
         */
        function parseDraft(raw) {
            if (raw.trim() === "") {
                return null
            }
            const parsed = Number(raw)
            if (!isFinite(parsed) || parsed < minimum || (!isNaN(maximum) && parsed > maximum)) {
                return null
            }
            return parsed
        }

        function commit() {
            const parsed = parseDraft(input.text)
            if (parsed === null) {
                input.text = String(value)
                return
            }
            input.text = String(parsed)
            if (parsed !== value) {
                committed(parsed)
            }
        }

        spacing: Theme.spaceXs
        onValueChanged: {
            if (!input.activeFocus) {
                input.text = String(value)
            }
        }

        TextField {
            id: input

            readonly property color fieldColor: !inline.isDraftValid ? Theme.danger
                                                                     : (inline.isActive ? Theme.accent : Theme.textDim)

            text: String(inline.value)
            color: fieldColor
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontBase - 1
            horizontalAlignment: Text.AlignHCenter
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            selectByMouse: true
            padding: 0
            leftPadding: 2
            rightPadding: 2
            implicitWidth: 52
            implicitHeight: 24
            Accessible.name: inline.tooltip
            ToolTip.visible: inline.tooltip.length > 0 && hovered
            ToolTip.text: inline.tooltip
            ToolTip.delay: 600
            onEditingFinished: inline.commit()

            background: Item {
                Rectangle {
                    anchors.fill: parent
                    visible: input.activeFocus
                    color: Theme.input
                }
                // 虛線底線
                Row {
                    anchors.bottom: parent.bottom
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 2
                    Repeater {
                        model: Math.floor(input.width / 5)
                        Rectangle { width: 3; height: 1; color: input.fieldColor }
                    }
                }
            }
        }
        Text {
            text: inline.unit
            color: Theme.textDim
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontSmall
        }
    }

    /// 編隊參數區：參數欄位（CONFIRM 位於上方隊形列）
    component FormBox: Rectangle {
        default property alias content: formColumn.data

        color: Theme.inset
        implicitHeight: formColumn.implicitHeight
        Layout.fillWidth: true

        ColumnLayout {
            id: formColumn

            width: parent.width
            spacing: 0
        }
    }

    /// 編隊參數列：標籤與行內欄位
    component FormField: RowLayout {
        property string label: ""
        default property alias field: slot.data

        spacing: Theme.spaceSm
        Layout.fillWidth: true
        Layout.leftMargin: Theme.spaceLg + 22
        Layout.rightMargin: Theme.spaceLg
        Layout.preferredHeight: 36

        Label {
            text: parent.label
            color: Theme.textMuted
            font.pixelSize: Theme.fontLabel
            font.letterSpacing: Theme.labelSpacing
        }
        Item { Layout.fillWidth: true }
        RowLayout { id: slot }
    }

    /// 確認列與安全列的小型外框按鈕（EXEC、CLR、DISARM）
    component ScratchButton: ActionButton {
        id: scratch

        /// 實心底色（EXEC 高亮）；文字色仍取 accentColor
        property bool isFilled: false
        property color fillColor: Theme.caution

        textAlignment: Text.AlignHCenter
        implicitHeight: 28
        leftPadding: Theme.spaceMd
        rightPadding: Theme.spaceMd
        topPadding: 0
        bottomPadding: 0
        font.family: Theme.fontMono
        font.pixelSize: Theme.fontLabel + 1
        font.letterSpacing: Theme.labelSpacing
        background: Rectangle {
            color: scratch.isFilled ? (scratch.down ? Qt.darker(scratch.fillColor, 1.25)
                                                    : (scratch.hovered ? Qt.lighter(scratch.fillColor, 1.12)
                                                                       : scratch.fillColor))
                                    : (scratch.down ? Theme.controlActive
                                                    : (scratch.hovered ? Theme.controlHover : "transparent"))
            border.width: scratch.visualFocus ? 2 : 1
            border.color: scratch.visualFocus ? Theme.accent : (scratch.isFilled ? scratch.fillColor
                                                                                 : scratch.accentColor)
        }
    }

    component MoveButton: ActionButton {
        textAlignment: Text.AlignHCenter
        implicitHeight: 40
        padding: 0
        Layout.fillWidth: true
        Layout.minimumWidth: 54
    }
}
