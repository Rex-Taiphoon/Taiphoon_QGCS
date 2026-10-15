/**
 * @file NumberField.qml
 * @brief 數字輸入列：編輯期間允許暫時留白，失焦時不合法則還原（網頁版 NumberInput）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: root

    /// 標籤
    property string label: ""
    /// 目前已確認的值
    property real value: 0
    /// 允許的最小值
    property real minimum: 0.1
    /// 允許的最大值；NaN 表示不限
    property real maximum: NaN
    /// 單位
    property string unit: "m"
    /// 提示文字
    property string tooltip: ""
    /// 目前輸入是否合法（供呼叫端停用送出按鈕）
    readonly property bool isDraftValid: parseDraft(field.text) !== null

    /// 使用者確認新值
    signal committed(real newValue)

    spacing: 8

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
        const parsed = parseDraft(field.text)
        if (parsed === null) {
            field.text = String(value)
            return
        }
        field.text = String(parsed)
        if (parsed !== value) {
            committed(parsed)
        }
    }

    onValueChanged: {
        if (!field.activeFocus) {
            field.text = String(value)
        }
    }

    Label {
        text: root.label
        color: Theme.textMuted
        font.pixelSize: Theme.fontBase
        Layout.minimumWidth: 88
    }

    TextField {
        id: field

        text: String(root.value)
        color: Theme.textBody
        font.family: Theme.fontMono
        font.pixelSize: Theme.fontBase
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        selectByMouse: true
        Layout.preferredWidth: 88
        Layout.preferredHeight: Theme.controlHeight
        background: Rectangle {
            color: Theme.input
            radius: Theme.radiusInput
            border.width: field.activeFocus ? 2 : 1
            border.color: root.isDraftValid ? (field.activeFocus ? Theme.accent : Theme.borderStrong) : Theme.danger
        }
        ToolTip.visible: root.tooltip.length > 0 && hovered
        ToolTip.text: root.tooltip
        ToolTip.delay: 600
        onEditingFinished: root.commit()
    }

    Label {
        text: root.unit
        color: Theme.textMuted
        font.pixelSize: Theme.fontBase
        Layout.preferredWidth: 28
    }
}
