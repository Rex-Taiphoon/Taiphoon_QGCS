/**
 * @file SiteSetupDialog.qml
 * @brief 網路設定（首次啟動精靈）：Agent 位址、網卡、Domain ID、multicast 與 participant index（F-13）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 *
 * 取代 Go 啟動器的本機設定頁；保存後立即以新設定重建 DDS，不需重新啟動程式。
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: root

    required property var settings
    required property var network

    /// 首次啟動時顯示說明文字
    property bool isFirstRun: false
    property string errorText: ""

    function loadFromSettings() {
        domainField.value = settings.domainId
        peersField.text = settings.peers.join("\n")
        multicastBox.checked = settings.isMulticastAllowed
        fixedIndexBox.checked = settings.participantIndex >= 0
        indexField.value = settings.participantIndex >= 0 ? settings.participantIndex : 0
        network.refreshInterfaces()
        interfaceBox.currentIndex = 0
        for (let i = 0; i < interfaceBox.model.length; ++i) {
            const item = interfaceBox.model[i]
            if (item.value === settings.networkInterface) {
                interfaceBox.currentIndex = i
            }
        }
        errorText = ""
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    closePolicy: isFirstRun ? Popup.NoAutoClose : (Popup.CloseOnEscape | Popup.CloseOnPressOutside)
    width: Math.min(620, parent ? parent.width * 0.92 : 620)
    padding: 24
    onAboutToShow: loadFromSettings()

    Overlay.modal: Rectangle {
        color: Qt.rgba(2 / 255, 8 / 255, 14 / 255, 0.78)
    }

    background: Rectangle {
        color: Theme.panel
        radius: Theme.radius
        border.color: Theme.border
    }

    contentItem: ColumnLayout {
        spacing: 14

        Label {
            text: root.isFirstRun ? qsTr("歡迎使用 Taiphoon_QGCS") : qsTr("網路設定")
            color: Theme.textStrong
            font.pixelSize: Theme.fontTitle
        }
        Label {
            visible: root.isFirstRun
            text: qsTr("請輸入現場 Agent 的位址。同一台電腦或允許 multicast 的區網中模擬時，可保留空白直接開始。")
            color: Theme.textMuted
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        GridLayout {
            columns: 2
            columnSpacing: 14
            rowSpacing: 10
            Layout.fillWidth: true

            Label { text: qsTr("Agent 位址"); color: Theme.textBody; font.pixelSize: Theme.fontBase; Layout.alignment: Qt.AlignTop }
            ColumnLayout {
                spacing: 4
                Layout.fillWidth: true
                TextArea {
                    id: peersField

                    placeholderText: qsTr("例如 192.168.0.101，多台以換行分隔")
                    color: Theme.textBody
                    font.pixelSize: Theme.fontBase
                    wrapMode: TextEdit.NoWrap
                    Layout.fillWidth: true
                    Layout.preferredHeight: 96
                    background: Rectangle {
                        color: Theme.input
                        radius: Theme.radiusInput
                        border.color: peersField.activeFocus ? Theme.accent : Theme.borderStrong
                    }
                }
                Label {
                    text: qsTr("Agent 所在電腦的區網 IP；也可在 Agent 端設定本機 IP 讓其主動探索")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }

            Label { text: qsTr("網卡"); color: Theme.textBody; font.pixelSize: Theme.fontBase }
            ComboBox {
                id: interfaceBox

                textRole: "label"
                valueRole: "value"
                font.pixelSize: Theme.fontBase
                Layout.fillWidth: true
                model: {
                    const items = [{"label": qsTr("自動選擇"), "value": ""}]
                    for (const entry of root.network.interfaces) {
                        items.push({"label": entry.label, "value": entry.address})
                    }
                    return items
                }
            }

            Label { text: "Domain ID"; color: Theme.textBody; font.pixelSize: Theme.fontBase }
            SpinBox {
                id: domainField

                from: 0
                to: 232
                editable: true
                font.pixelSize: Theme.fontBase
            }

            Label { text: "Multicast"; color: Theme.textBody; font.pixelSize: Theme.fontBase }
            CheckBox {
                id: multicastBox

                text: qsTr("允許 multicast（本機或區網模擬；Wi-Fi 現場建議關閉）")
                font.pixelSize: Theme.fontSmall
                Layout.fillWidth: true
            }

            Label { text: "Participant index"; color: Theme.textBody; font.pixelSize: Theme.fontBase }
            RowLayout {
                CheckBox {
                    id: fixedIndexBox

                    text: qsTr("固定")
                    font.pixelSize: Theme.fontSmall
                }
                SpinBox {
                    id: indexField

                    enabled: fixedIndexBox.checked
                    from: 0
                    to: 119
                    editable: true
                    font.pixelSize: Theme.fontBase
                }
                Label {
                    text: qsTr("同機已有其他 DDS 程式占用埠號時使用")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
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
            spacing: 12
            Layout.alignment: Qt.AlignRight

            ActionButton {
                visible: !root.isFirstRun
                text: qsTr("取消")
                textAlignment: Text.AlignHCenter
                onClicked: root.close()
            }
            ActionButton {
                text: root.isFirstRun ? qsTr("開始使用") : qsTr("保存並重新連線")
                textAlignment: Text.AlignHCenter
                accentColor: Theme.accent
                onClicked: {
                    const error = root.network.applySite(domainField.value, interfaceBox.currentValue, peersField.text,
                                                         multicastBox.checked,
                                                         fixedIndexBox.checked ? indexField.value : -1)
                    root.errorText = error
                    if (error.length === 0) {
                        root.close()
                    }
                }
            }
        }
    }
}
