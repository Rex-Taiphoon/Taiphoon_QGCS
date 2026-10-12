/**
 * @file SlideToConfirm.qml
 * @brief L2 防呆：拖曳滑桿到底才觸發（Arm），鍵盤需連按兩次 Enter（ADR-0006）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
import QtQuick

Item {
    id: root

    /// 說明文字
    property string label: ""
    /// 強調色
    property color accentColor: "#ff4444"
    /// 滑動進度 0～1
    property real progress: 0
    /// 已確認（觸發後保持，直到停用或重設）
    property bool isConfirmed: false
    /// 鍵盤第一次確認後等待第二次
    property bool isKeyboardArmed: false

    /// 滑到底時觸發
    signal confirmed()

    /// 觸發門檻：避免拖曳到接近底端時的誤差
    readonly property real confirmThreshold: 0.98
    readonly property int knobWidth: 50

    function reset() {
        progress = 0
        isConfirmed = false
        isKeyboardArmed = false
    }

    function confirmNow() {
        progress = 1
        isConfirmed = true
        isKeyboardArmed = false
        confirmed()
    }

    implicitHeight: 52
    implicitWidth: 300
    opacity: enabled ? 1.0 : Theme.disabledOpacity
    activeFocusOnTab: enabled
    onEnabledChanged: reset()

    Accessible.role: Accessible.Slider
    Accessible.name: label

    Keys.onPressed: (event) => {
        if (!enabled || isConfirmed) {
            return
        }
        if (event.key === Qt.Key_Space || event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            event.accepted = true
            if (isKeyboardArmed) {
                confirmNow()
            } else {
                isKeyboardArmed = true
                progress = 0.5
            }
        } else if (event.key === Qt.Key_Escape) {
            event.accepted = true
            reset()
        }
    }

    Rectangle {
        id: track

        anchors.fill: parent
        radius: height / 2
        color: "#1a1a1a"
        border.width: 2
        border.color: root.accentColor
        clip: true

        Rectangle {
            width: root.knobWidth / 2 + root.progress * (track.width - root.knobWidth)
            height: parent.height
            radius: parent.radius
            color: root.isConfirmed ? root.accentColor : Qt.alpha(root.accentColor, 0.53)
        }

        Text {
            anchors.centerIn: parent
            text: root.isConfirmed ? qsTr("已確認") : (root.isKeyboardArmed ? qsTr("再按 Enter 確認") : root.label)
            color: Theme.textBody
            font.pixelSize: Theme.fontBase
        }
    }

    Rectangle {
        id: knob

        x: 2 + root.progress * (root.width - root.knobWidth - 4)
        y: 2
        width: root.knobWidth - 4
        height: root.height - 4
        radius: height / 2
        color: root.isConfirmed ? root.accentColor : "#333333"
        border.width: 2
        border.color: root.accentColor

        Text {
            anchors.centerIn: parent
            text: root.isConfirmed ? "✓" : (root.isKeyboardArmed ? "⏎" : "→")
            color: "white"
            font.pixelSize: 22
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: -3
        radius: height / 2
        color: "transparent"
        border.width: root.activeFocus || root.isKeyboardArmed ? 2 : 0
        border.color: root.accentColor
    }

    MouseArea {
        anchors.fill: parent
        enabled: root.enabled && !root.isConfirmed

        function update(mouseX) {
            const travel = root.width - root.knobWidth - 4
            const raw = mouseX - root.knobWidth / 2
            root.progress = travel > 0 ? Math.max(0, Math.min(raw, travel)) / travel : 0
            if (root.progress >= root.confirmThreshold) {
                root.confirmNow()
            }
        }

        onPressed: (mouse) => {
            // 只能從滑塊開始拖曳，點擊軌道其他位置不會直接跳到底
            if (mouse.x > knob.x + knob.width + 8) {
                mouse.accepted = false
            }
        }
        onPositionChanged: (mouse) => update(mouse.x)
        onReleased: {
            if (!root.isConfirmed) {
                root.progress = 0
            }
        }
        onCanceled: root.progress = 0
    }
}
