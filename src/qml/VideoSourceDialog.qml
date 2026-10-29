/**
 * @file VideoSourceDialog.qml
 * @brief 影像串流網址設定：機群表右鍵開啟，編輯單機網址或機群網址範本，關閉時自動套用（ADR-0019）
 * @author Taiphoon GCS Team
 * @date 2026-10-29
 *
 * 開啟時顯示目前的串流網址。以 Esc、點選視窗外、Enter 或「完成」關閉時套用；
 * 內容不合法時重新開啟並保留輸入，修正或按「還原」後才能關閉，不會悄悄丟掉修改。
 * 驗證與「清空即改用機群網址」等規則在 VideoController（C++）。
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: root

    required property var bridge
    required property var video

    /// 編輯對象：uav_N_agent；空字串表示機群網址範本
    property string agent: ""
    /// 開啟時的內容，供「還原」使用
    property string originalText: ""
    /// 單機：目前網址的來源說明
    property string sourceHint: ""
    property string errorText: ""
    /// 套用失敗後重新開啟時保留輸入
    property bool isKeepingInput: false

    readonly property bool isFleet: agent.length === 0
    readonly property string shortName: agent.replace(/_agent$/, "").toUpperCase()

    /**
     * @brief 開啟設定
     * @param target uav_N_agent；空字串表示機群網址範本
     */
    function openFor(target) {
        root.agent = target
        root.isKeepingInput = false
        root.open()
    }

    /** @brief 以目前設定填入欄位 */
    function load() {
        const fleetUrl = video.previewUrl(video.urlTemplate, agent)
        if (isFleet) {
            originalText = video.urlTemplate
        } else {
            originalText = video.urlFor(agent)
            if (video.hasAgentUrl(agent)) {
                sourceHint = fleetUrl.length > 0
                        ? qsTr("這台使用單機網址；清空後改用機群網址：%1").arg(fleetUrl)
                        : qsTr("這台使用單機網址；目前沒有機群網址，清空後不播放影像。")
            } else if (fleetUrl.length > 0) {
                sourceHint = qsTr("目前使用機群網址；修改後成為這台的單機網址，不影響其他無人機。")
            } else {
                sourceHint = qsTr("尚未設定。也可在機群表的「全部」按右鍵設定機群網址，一次套用到所有無人機。")
            }
        }
        urlField.text = originalText
        errorText = ""
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: Math.min(600, parent ? parent.width * 0.92 : 600)
    padding: Theme.spaceXl

    onAboutToShow: {
        if (!isKeepingInput) {
            load()
        }
        isKeepingInput = false
    }
    onOpened: urlField.forceActiveFocus()
    // 關閉即套用；不合法時重新開啟並保留輸入
    onClosed: {
        const error = isFleet ? video.setUrlTemplate(urlField.text) : video.setAgentUrl(agent, urlField.text)
        if (error.length > 0) {
            errorText = qsTr("%1。請修正後再關閉，或按「還原」放棄修改。").arg(error)
            isKeepingInput = true
            Qt.callLater(root.open)
        }
    }

    Overlay.modal: Rectangle {
        color: Qt.rgba(2 / 255, 8 / 255, 14 / 255, 0.78)
    }

    background: Rectangle {
        color: Theme.panel
        radius: Theme.radius
        border.color: Theme.border
    }

    contentItem: ColumnLayout {
        spacing: Theme.spaceMd

        RowLayout {
            spacing: Theme.spaceMd
            Layout.fillWidth: true

            Label {
                text: root.isFleet ? qsTr("機群影像串流") : qsTr("%1 影像串流").arg(root.shortName)
                color: Theme.textStrong
                font.pixelSize: Theme.fontTitle
                Layout.fillWidth: true
            }
            ActionButton {
                iconName: "close"
                accentColor: Theme.textMuted
                implicitWidth: 32
                implicitHeight: 32
                padding: 0
                background: Item {}
                tooltip: qsTr("關閉並套用")
                Accessible.name: tooltip
                onClicked: root.close()
            }
        }

        Label {
            text: root.isFleet ? qsTr("機群網址") : qsTr("串流網址")
            color: Theme.textBody
            font.pixelSize: Theme.fontSmall
        }
        TextField {
            id: urlField

            placeholderText: root.isFleet ? qsTr("例如 rtsp://192.168.0.10:8554/uav_{N}")
                                          : qsTr("例如 rtsp://192.168.0.13:554/stream1")
            color: Theme.textBody
            font.family: Theme.fontMono
            font.pixelSize: Theme.fontBase
            selectByMouse: true
            Layout.fillWidth: true
            Accessible.name: root.isFleet ? qsTr("機群影像網址") : qsTr("%1 影像網址").arg(root.shortName)
            background: Rectangle {
                color: Theme.input
                radius: Theme.radiusInput
                border.color: root.errorText.length > 0 ? Theme.danger
                                                        : (urlField.activeFocus ? Theme.accent : Theme.borderStrong)
            }
            onAccepted: root.close()
        }
        Label {
            text: root.isFleet ? qsTr("{N} 代入無人機編號、{agent} 代入 uav_N_agent，套用到每一台上線的無人機；"
                                      + "支援 rtsp、udp、srt、rtmp 與 http。設定了單機網址的無人機不受影響，清空則不使用機群網址。")
                               : root.sourceHint
            color: Theme.textMuted
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        // 機群：預覽每台線上無人機實際使用的網址
        ColumnLayout {
            visible: root.isFleet && root.bridge.onlineCount > 0
            spacing: Theme.spaceXs
            Layout.fillWidth: true
            Layout.topMargin: Theme.spaceXs

            Label {
                text: qsTr("線上無人機")
                color: Theme.textMuted
                font.pixelSize: Theme.fontLabel
                font.letterSpacing: Theme.labelSpacing
            }
            Repeater {
                // 每次開啟重新建立，單機網址的標示才會是最新狀態
                model: root.isFleet && root.visible ? root.bridge.fleetModel : null
                delegate: RowLayout {
                    id: previewRow

                    required property string agent

                    readonly property bool hasAgentUrl: root.video.hasAgentUrl(agent)
                    readonly property string url: hasAgentUrl ? root.video.urlFor(agent)
                                                              : root.video.previewUrl(urlField.text, agent)

                    spacing: Theme.spaceMd
                    Layout.fillWidth: true

                    Label {
                        text: previewRow.agent.replace(/_agent$/, "").toUpperCase()
                        color: Theme.textStrong
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontSmall
                        Layout.preferredWidth: 64
                    }
                    Label {
                        text: previewRow.url.length > 0 ? previewRow.url : qsTr("未設定")
                        color: previewRow.url.length > 0 ? Theme.textValue : Theme.textMuted
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontSmall
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                    Label {
                        visible: previewRow.hasAgentUrl
                        text: qsTr("單機網址")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontLabel
                    }
                }
            }
        }

        Label {
            visible: root.errorText.length > 0
            text: root.errorText
            color: Theme.danger
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        RowLayout {
            spacing: Theme.spaceMd
            Layout.fillWidth: true
            Layout.topMargin: Theme.spaceXs

            Label {
                text: qsTr("關閉視窗時自動套用")
                color: Theme.textMuted
                font.pixelSize: Theme.fontSmall
                Layout.fillWidth: true
            }
            ActionButton {
                text: qsTr("還原")
                textAlignment: Text.AlignHCenter
                enabled: urlField.text !== root.originalText || root.errorText.length > 0
                onClicked: {
                    urlField.text = root.originalText
                    root.errorText = ""
                }
            }
            ActionButton {
                text: qsTr("完成")
                textAlignment: Text.AlignHCenter
                accentColor: Theme.accent
                onClicked: root.close()
            }
        }
    }
}
