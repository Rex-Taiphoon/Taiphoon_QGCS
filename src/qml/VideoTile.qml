/**
 * @file VideoTile.qml
 * @brief 單台無人機的影像縮圖：擁有該機的 VideoPlayer 與獨立視窗，點選即選取該機，雙擊或按右上角開啟獨立視窗
 * @author Taiphoon GCS Team
 * @date 2026-10-29
 *
 * 播放器屬於縮圖而不是視窗：縮圖與獨立視窗共用同一路連線。
 * 只要面板展開或獨立視窗開啟就播放，兩者都關閉時停止，釋放機上串流伺服器與無線鏈路頻寬。
 */
import QtQuick
import QtQuick.Controls
import QtMultimedia
import TaiphoonQgcs.Video

Rectangle {
    id: tile

    // fleetModel 角色；model 為整列資料，交給獨立視窗顯示完整狀態
    required property var model
    required property string agent
    required property bool isSelected

    property var bridge: null
    property var video: null
    /// 影像面板是否展開
    property bool isPanelOpen: false

    /// 大寫短名稱（去掉固定的 _agent 字尾）
    readonly property string shortName: agent.replace(/_agent$/, "").toUpperCase()

    /** @brief 依目前設定更新串流 URL；設定改變時由 VideoController.changed 觸發 */
    function refreshSource() {
        player.source = tile.video !== null ? tile.video.urlFor(tile.agent) : ""
    }

    /** @brief 開啟（或帶到最前面）獨立視窗 */
    function openWindow() {
        videoWindow.show()
        videoWindow.raise()
        videoWindow.requestActivate()
    }

    color: "black"
    border.width: 1
    border.color: isSelected ? Theme.accent : Theme.border

    Component.onCompleted: refreshSource()

    Connections {
        target: tile.video
        function onChanged() { tile.refreshSource() }
    }

    VideoPlayer {
        id: player

        active: tile.isPanelOpen || videoWindow.visible
    }

    VideoOutput {
        id: output

        anchors.fill: parent
        anchors.margins: 1
        fillMode: VideoOutput.PreserveAspectFit

        Component.onCompleted: player.attachOutput(output.videoSink)
        Component.onDestruction: player.detachOutput(output.videoSink)
    }

    // 沒有畫面時的狀態（連線中、中斷、未設定）；已在獨立視窗播放時也標示，避免以為縮圖壞了
    Label {
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.spaceSm
        visible: !player.isPlaying
        text: player.statusText
        color: player.statusText.indexOf("中斷") >= 0 ? Theme.caution : Theme.textMuted
        font.pixelSize: Theme.fontLabel
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
    }

    // 左上角：機號（選取機以強調色標示）
    Rectangle {
        x: 1
        y: 1
        width: nameLabel.implicitWidth + 2 * Theme.spaceSm
        height: nameLabel.implicitHeight + Theme.spaceXs
        color: Qt.rgba(0, 0, 0, 0.6)

        Label {
            id: nameLabel

            anchors.centerIn: parent
            text: tile.shortName
            color: tile.isSelected ? Theme.accent : Theme.textStrong
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontLabel
        }
    }

    // 右上角：開啟獨立視窗
    ActionButton {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 1
        iconName: "popOut"
        accentColor: videoWindow.visible ? Theme.accent : Theme.textBody
        implicitWidth: 28
        implicitHeight: 28
        padding: 0
        background: Rectangle {
            color: parent.hovered ? Theme.controlHover : Qt.rgba(0, 0, 0, 0.6)
        }
        tooltip: qsTr("在獨立視窗播放 %1 影像").arg(tile.shortName)
        Accessible.name: tooltip
        onClicked: tile.openWindow()
    }

    // 點選選取該機（與機群表格相同）；雙擊開啟獨立視窗
    TapHandler {
        onTapped: tile.bridge.selectAgent(tile.agent)
        onDoubleTapped: tile.openWindow()
    }

    HoverHandler {
        id: hover
    }

    ToolTip.visible: hover.hovered && player.errorText.length > 0
    ToolTip.text: player.errorText
    ToolTip.delay: 600

    VideoWindow {
        id: videoWindow

        player: player
        telemetry: tile.model
        shortName: tile.shortName
        isSelected: tile.isSelected
    }
}
