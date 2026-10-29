/**
 * @file VideoWindow.qml
 * @brief 單台無人機的獨立影像視窗：可移到其他螢幕、調整大小或全螢幕，疊加顯示該機完整狀態
 * @author Taiphoon GCS Team
 * @date 2026-10-29
 *
 * 上方：機號、任務階段、PX4 飛行模式、PX4 解鎖／落地狀態與電量、電壓、電流（與主畫面狀態列相同的儀表格）；
 * 下方：高度、NED、經緯度、航向、Roll、Pitch，左下角姿態儀。資料取自 fleetModel 該機的列，不限於選取機。
 * 不設定 transientParent，視窗可被主視窗遮住、也可單獨最小化；主視窗關閉時程式一併結束（Main.qml）。
 * 畫面來自縮圖的 VideoPlayer，不另開連線。雙擊或 F11 切換全螢幕，Esc 離開全螢幕。
 */
import QtQuick
import QtQuick.Controls
import QtMultimedia

ApplicationWindow {
    id: root

    required property var player
    /// fleetModel 中該機的列（model 物件），角色見 FleetModel::roleNames
    required property var telemetry
    property string shortName: ""
    property bool isSelected: false

    readonly property bool isFullScreen: visibility === Window.FullScreen
    readonly property bool isWaiting: telemetry.isWaitingForFlightController
    /// 疊加資訊列的底色：半透明，影像仍可辨識
    readonly property color overlayColor: Qt.rgba(10 / 255, 12 / 255, 13 / 255, 0.72)
    /// 資訊列每一行的高度（與主畫面狀態列相同）
    readonly property int cellHeight: 48

    /** @brief 切換全螢幕 */
    function toggleFullScreen() {
        if (root.isFullScreen) {
            root.showNormal()
        } else {
            root.showFullScreen()
        }
    }

    /**
     * @brief 數值附暗色單位（StyledText，與狀態列相同）
     * @param value 數值文字
     * @param unit 單位
     * @return StyledText 字串
     */
    function withUnit(value, unit) {
        return (value || "—") + "<font color=\"" + Theme.textDim + "\">" + unit + "</font>"
    }

    /**
     * @brief 帶正負號的角度文字（負號使用 U+2212）
     * @param value 角度（度）
     * @return 例如 "+1.6°"
     */
    function signedDegrees(value) {
        const text = Math.abs(value).toFixed(1)
        return (value < 0 && Number(text) !== 0 ? "−" : "+") + text + "°"
    }

    transientParent: null
    width: 1024
    height: 576
    minimumWidth: 480
    minimumHeight: 270
    title: qsTr("%1 影像 — Taiphoon_QGCS").arg(shortName)
    color: "black"
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontBase
    palette.toolTipBase: Theme.panelDeep
    palette.toolTipText: Theme.text

    Component.onCompleted: root.player.attachOutput(output.videoSink)
    Component.onDestruction: root.player.detachOutput(output.videoSink)

    VideoOutput {
        id: output

        anchors.fill: parent
        fillMode: VideoOutput.PreserveAspectFit
    }

    // 鍵盤：F11 切換全螢幕、Esc 離開全螢幕
    Item {
        anchors.fill: parent
        focus: true
        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_F11) {
                root.toggleFullScreen()
                event.accepted = true
            } else if (event.key === Qt.Key_Escape && root.isFullScreen) {
                root.showNormal()
                event.accepted = true
            }
        }

        TapHandler {
            onDoubleTapped: root.toggleFullScreen()
        }
    }

    Label {
        anchors.centerIn: parent
        visible: !root.player.isPlaying
        text: root.player.statusText + (root.player.errorText.length > 0 ? "\n" + root.player.errorText : "")
        color: root.player.statusText.indexOf("中斷") >= 0 ? Theme.caution : Theme.textMuted
        font.pixelSize: Theme.fontBase
        horizontalAlignment: Text.AlignHCenter
    }

    // 上方：識別、模式與電力；視窗太窄時換行，不截斷數值
    Rectangle {
        id: topBar

        width: parent.width
        height: Math.max(root.cellHeight, topFlow.implicitHeight)
        color: root.overlayColor

        Flow {
            id: topFlow

            width: parent.width - topControls.width - Theme.spaceXs

            Cell {
                label: "UAV"
                value: root.shortName
                valueColor: root.isSelected ? Theme.accent : Theme.textStrong
            }
            Cell {
                visible: root.isWaiting
                label: "STATUS"
                value: qsTr("等待飛控連線中...")
                valueColor: Theme.caution
            }
            Cell {
                visible: !root.isWaiting
                label: "MODE"
                value: root.telemetry.modeLabel
                valueColor: Theme.normal
                isBoxed: true
            }
            Cell {
                visible: !root.isWaiting
                label: "PX4 MODE"
                value: root.telemetry.px4ModeLabel
                valueColor: Theme.normal
            }
            Cell {
                visible: !root.isWaiting
                label: "PX4"
                value: root.telemetry.px4StatusLabel
            }
            Cell {
                visible: !root.isWaiting
                label: "BATT"
                value: root.withUnit(root.telemetry.batteryText, "%") + " "
                       + root.withUnit(root.telemetry.voltageText, "V") + " "
                       + root.withUnit(root.telemetry.currentText, "A")
                isStyled: true
            }
        }
        Row {
            id: topControls

            anchors.right: parent.right
            anchors.rightMargin: Theme.spaceXs
            height: root.cellHeight
            spacing: Theme.spaceSm

            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: root.player.resolutionText
                color: Theme.textMuted
                font.family: Theme.fontMono
                font.pixelSize: Theme.fontLabel
            }
            ActionButton {
                anchors.verticalCenter: parent.verticalCenter
                iconName: "fullscreen"
                accentColor: Theme.textBody
                implicitWidth: 40
                implicitHeight: 40
                padding: 0
                background: Item {}
                tooltip: root.isFullScreen ? qsTr("離開全螢幕（Esc）") : qsTr("全螢幕（F11 或雙擊畫面）")
                Accessible.name: tooltip
                onClicked: root.toggleFullScreen()
            }
        }
    }

    // 左下：姿態儀（姿態資料無效時不顯示）
    Rectangle {
        visible: !root.isWaiting && root.telemetry.hasAttitude
        anchors.left: parent.left
        anchors.bottom: bottomBar.top
        anchors.margins: Theme.spaceMd
        width: 112
        height: 112
        radius: width / 2
        color: root.overlayColor

        AttitudeIndicator {
            anchors.fill: parent
            anchors.margins: Theme.spaceXs
            roll: root.telemetry.roll
            pitch: root.telemetry.pitch
        }
    }

    // 下方：位置與姿態數值
    Rectangle {
        id: bottomBar

        visible: !root.isWaiting
        anchors.bottom: parent.bottom
        width: parent.width
        height: bottomFlow.implicitHeight
        color: root.overlayColor

        Flow {
            id: bottomFlow

            width: parent.width

            Cell { label: "ALT"; value: root.withUnit(root.telemetry.altitudeText, " m"); isStyled: true }
            Cell { label: "NED  N / E / D"; value: root.telemetry.nedText }
            Cell { label: "LAT / LON"; value: root.telemetry.positionText }
            Cell {
                label: "HDG"
                value: root.telemetry.hasHeading
                       ? String(Math.round(root.telemetry.heading) % 360).padStart(3, "0") + "°" : "—"
            }
            Cell {
                label: "ROLL"
                value: root.telemetry.hasAttitude ? root.signedDegrees(root.telemetry.roll) : "—"
            }
            Cell {
                label: "PITCH"
                value: root.telemetry.hasAttitude ? root.signedDegrees(root.telemetry.pitch) : "—"
            }
        }
    }

    /// 儀表格：上方小字標籤、下方等寬數值，右側細分隔線（與主畫面狀態列相同）
    component Cell: Item {
        id: cell

        property string label: ""
        property string value: ""
        property color valueColor: Theme.textValue
        /// 數值加外框（目前模式，比照 PFD 的 FMA 方框）
        property bool isBoxed: false
        /// 數值含 StyledText 標記（單位以暗色顯示）
        property bool isStyled: false

        width: Math.max(labelText.implicitWidth, valueBox.width) + 2 * Theme.spaceLg
        height: root.cellHeight

        Column {
            anchors.verticalCenter: parent.verticalCenter
            x: Theme.spaceLg
            spacing: 2

            Label {
                id: labelText

                text: cell.label
                color: Theme.textMuted
                font.pixelSize: Theme.fontLabel
                font.letterSpacing: Theme.labelSpacing
            }
            Rectangle {
                id: valueBox

                width: valueText.implicitWidth + (cell.isBoxed ? 10 : 0)
                height: valueText.implicitHeight
                x: cell.isBoxed ? -5 : 0
                color: "transparent"
                border.width: cell.isBoxed ? 1 : 0
                border.color: cell.valueColor

                Label {
                    id: valueText

                    anchors.centerIn: parent
                    text: cell.value
                    textFormat: cell.isStyled ? Text.StyledText : Text.PlainText
                    color: cell.valueColor
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontBase
                }
            }
        }
        Rectangle {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: 1
            height: parent.height * 0.6
            color: Theme.borderSoft
        }
    }
}
