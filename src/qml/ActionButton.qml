/**
 * @file ActionButton.qml
 * @brief 命令按鈕：以強調色區分命令種類，停用時半透明（網頁版 styles.btn）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
import QtQuick
import QtQuick.Controls

Button {
    id: control

    /// 邊框與文字的強調色
    property color accentColor: Theme.textBody
    /// 文字對齊；命令按鈕靠左，方向鍵置中
    property int textAlignment: Text.AlignLeft
    /// 提示文字
    property string tooltip: ""
    /// 非空時以向量圖示取代文字（見 Icon.qml）
    property string iconName: ""

    implicitHeight: 48
    padding: 10
    leftPadding: 14
    rightPadding: 14
    opacity: enabled ? 1.0 : Theme.disabledOpacity
    font.pixelSize: Theme.fontBase
    hoverEnabled: true

    ToolTip.visible: tooltip.length > 0 && hovered
    ToolTip.text: tooltip
    ToolTip.delay: 600

    contentItem: Item {
        implicitWidth: control.iconName.length > 0 ? 22 : label.implicitWidth
        implicitHeight: control.iconName.length > 0 ? 22 : label.implicitHeight

        Text {
            id: label

            anchors.fill: parent
            visible: control.iconName.length === 0
            text: control.text
            font: control.font
            color: control.accentColor
            horizontalAlignment: control.textAlignment
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        Icon {
            anchors.centerIn: parent
            width: 22
            height: 22
            visible: control.iconName.length > 0
            name: control.iconName
            color: control.accentColor
        }
    }

    background: Rectangle {
        radius: 8
        color: control.down ? Theme.controlActive : Theme.control
        border.width: control.visualFocus ? 3 : 1
        border.color: control.visualFocus ? Theme.accent
                                          : (control.accentColor === Theme.textBody ? Theme.border
                                                                                    : control.accentColor)
    }
}
