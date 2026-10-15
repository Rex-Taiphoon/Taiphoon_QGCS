/**
 * @file ActionButton.qml
 * @brief 命令按鈕：直角、細框，文字色表達風險等級（一般／需確認／危險），停用時半透明
 * @author Taiphoon GCS Team
 * @date 2026-10-15
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id: control

    /// 文字、圖示與邊框的強調色；預設為一般命令
    property color accentColor: Theme.commandNormal
    /// 文字對齊；命令按鈕靠左，方向鍵置中
    property int textAlignment: Text.AlignLeft
    /// 提示文字
    property string tooltip: ""
    /// 非空時以向量圖示取代文字（見 Icon.qml）
    property string iconName: ""
    /// 非空時在文字左側顯示向量圖示（取代原本的 Emoji）
    property string leadingIcon: ""
    /// 右側附註（例如「需確認」），以小字顯示
    property string note: ""

    readonly property bool isIconOnly: iconName.length > 0
    /// 一般命令使用中性框線，其他等級以強調色淡化後作框線
    readonly property color frameColor: accentColor === Theme.commandNormal ? Theme.border
                                                                            : Qt.alpha(accentColor, 0.55)

    implicitHeight: Theme.controlHeight
    padding: Theme.spaceSm
    leftPadding: Theme.spaceMd
    rightPadding: Theme.spaceMd
    opacity: enabled ? 1.0 : Theme.disabledOpacity
    font.pixelSize: Theme.fontBase
    hoverEnabled: true

    ToolTip.visible: tooltip.length > 0 && hovered
    ToolTip.text: tooltip
    ToolTip.delay: 600

    contentItem: Item {
        // 寬度只取自文字本身，不可反向依賴按鈕寬度，否則外層 Layout 會陷入無限重排
        implicitWidth: control.isIconOnly ? 20 : row.implicitWidth
        implicitHeight: control.isIconOnly ? 20 : row.implicitHeight

        RowLayout {
            id: row

            readonly property bool isCentered: control.textAlignment === Text.AlignHCenter

            visible: !control.isIconOnly
            anchors.verticalCenter: parent.verticalCenter
            x: isCentered ? Math.max(0, (parent.width - width) / 2) : 0
            width: isCentered ? Math.min(implicitWidth, parent.width) : parent.width
            spacing: Theme.spaceSm

            Icon {
                visible: control.leadingIcon.length > 0
                lineWidth: 1.8
                name: control.leadingIcon
                color: control.accentColor
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
            }
            Text {
                text: control.text
                font: control.font
                color: control.accentColor
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Text {
                visible: control.note.length > 0
                text: control.note
                color: control.accentColor
                opacity: 0.8
                font.pixelSize: Theme.fontLabel
                font.letterSpacing: Theme.labelSpacing
            }
        }
        Icon {
            anchors.centerIn: parent
            width: 20
            height: 20
            visible: control.isIconOnly
            name: control.iconName
            color: control.accentColor
        }
    }

    background: Rectangle {
        radius: Theme.radius
        color: control.down ? Theme.controlActive : (control.hovered ? Theme.controlHover : Theme.control)
        border.width: control.visualFocus ? 2 : 1
        border.color: control.visualFocus ? Theme.accent : control.frameColor
    }
}
