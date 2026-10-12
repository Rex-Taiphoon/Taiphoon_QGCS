/**
 * @file MapView.qml
 * @brief 全幅地圖：OSM 點陣圖磚、依航向旋轉的箭頭與名稱、EGO-Swarm 軌跡、自動跟隨（ADR-0016）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Shapes
import QtLocation
import QtPositioning

Item {
    id: root

    required property var bridge
    required property var settings

    /// 尚未取得有效 GPS 時的預設中心（台北），與網頁版相同
    readonly property var defaultCenter: QtPositioning.coordinate(25.03997, 121.51195)
    readonly property real defaultZoom: 16
    /// 上次自動置中的目標與位置；使用者拖曳後清除，直到位置改變才再次置中（網頁版行為）
    property string lastAutoCenterKey: ""

    function followSelected() {
        if (!settings.autoFollow || !bridge.isSingleSelected) {
            if (!bridge.isSingleSelected) {
                lastAutoCenterKey = ""
            }
            return
        }
        const summary = bridge.selectedSummary
        if (!summary.hasPosition) {
            return
        }
        const key = bridge.selectedAgent + ":" + summary.latitude.toFixed(7) + "," + summary.longitude.toFixed(7)
        if (key === lastAutoCenterKey) {
            return
        }
        lastAutoCenterKey = key
        map.center = QtPositioning.coordinate(summary.latitude, summary.longitude)
    }

    Connections {
        target: root.bridge
        function onTelemetryChanged() { root.followSelected() }
        function onSelectionChanged() { root.followSelected() }
    }

    Plugin {
        id: osmPlugin

        name: "osm"
        // OSM 圖磚使用政策要求可識別的 User-Agent
        PluginParameter { name: "osm.useragent"; value: "Taiphoon_QGCS/" + Qt.application.version }
        PluginParameter { name: "osm.mapping.custom.host"; value: "https://tile.openstreetmap.org/%z/%x/%y.png" }
        PluginParameter { name: "osm.mapping.custom.mapcopyright"; value: "OpenStreetMap" }
        PluginParameter { name: "osm.mapping.providersrepository.disabled"; value: true }
        PluginParameter { name: "osm.mapping.highdpi_tiles"; value: false }
    }

    Map {
        id: map

        property var startCentroid

        anchors.fill: parent
        plugin: osmPlugin
        center: root.defaultCenter
        zoomLevel: root.defaultZoom
        copyrightsVisible: false
        color: Theme.background

        Component.onCompleted: {
            // 「Custom URL Map」為自訂圖磚來源，位於清單最後
            for (let i = 0; i < supportedMapTypes.length; ++i) {
                if (supportedMapTypes[i].style === MapType.CustomMap) {
                    activeMapType = supportedMapTypes[i]
                }
            }
        }

        // 軌跡在標記下方
        MapItemView {
            model: root.bridge.trajectories
            delegate: MapPolyline {
                required property var modelData

                line.width: 3
                line.color: modelData.isSelected ? Theme.markerSelected : Theme.trajectoryOther
                opacity: 0.9
                path: modelData.path.map(p => QtPositioning.coordinate(p.latitude, p.longitude))
            }
        }

        MapItemView {
            model: root.bridge.fleetModel
            delegate: MapQuickItem {
                id: marker

                required property string agent
                required property bool isSelected
                required property bool hasPosition
                required property real latitude
                required property real longitude
                required property bool hasHeading
                required property real heading
                required property string modeLabel

                readonly property color markerColor: isSelected ? Theme.markerSelected : Theme.markerNormal
                readonly property int arrowSize: isSelected ? 38 : 32

                visible: hasPosition
                // 選中的標記畫在最上層，避免與鄰近無人機重疊時被遮住
                z: isSelected ? 1 : 0
                coordinate: QtPositioning.coordinate(latitude, longitude)
                anchorPoint.x: sourceItem.width / 2
                anchorPoint.y: arrowSize / 2
                zoomLevel: 0

                sourceItem: Item {
                    width: Math.max(marker.arrowSize, nameLabel.width)
                    height: marker.arrowSize + 7 + nameLabel.height

                    Shape {
                        id: arrow

                        x: (parent.width - marker.arrowSize) / 2
                        width: marker.arrowSize
                        height: marker.arrowSize
                        preferredRendererType: Shape.CurveRenderer
                        // 箭頭原形指向右方；航向 0 度（北）需旋轉 -90 度
                        rotation: (marker.hasHeading ? marker.heading : 0) - map.bearing - 90

                        ShapePath {
                            fillColor: marker.markerColor
                            strokeColor: Qt.rgba(0, 0, 0, 0.6)
                            strokeWidth: 1
                            startX: 0
                            startY: 0
                            PathLine { x: arrow.width; y: arrow.height / 2 }
                            PathLine { x: 0; y: arrow.height }
                            PathLine { x: arrow.width * 0.28; y: arrow.height / 2 }
                            PathLine { x: 0; y: 0 }
                        }
                    }

                    Rectangle {
                        id: nameLabel

                        anchors.horizontalCenter: parent.horizontalCenter
                        y: marker.arrowSize + 7
                        width: nameText.implicitWidth + 10
                        height: nameText.implicitHeight + 4
                        radius: 4
                        color: Qt.rgba(7 / 255, 14 / 255, 22 / 255, 0.86)
                        border.color: marker.markerColor

                        Text {
                            id: nameText

                            anchors.centerIn: parent
                            text: marker.agent
                            color: Theme.textStrong
                            font.pixelSize: 12
                            font.bold: true
                        }
                    }

                    TapHandler {
                        onTapped: root.bridge.selectAgent(marker.agent)
                    }
                    HoverHandler { id: markerHover }
                    ToolTip.visible: markerHover.hovered
                    ToolTip.text: marker.agent + " - " + marker.modeLabel
                                  + (marker.hasHeading ? qsTr("，航向 %1 度").arg(Math.round(marker.heading) % 360)
                                                       : qsTr("，航向資料無效"))
                }
            }
        }

        // Qt 6 的 Map 不含手勢，依 Qt 範例以指標處理器實作縮放、旋轉與平移
        PinchHandler {
            id: pinch

            target: null
            grabPermissions: PointerHandler.TakeOverForbidden
            onActiveChanged: if (active) {
                map.startCentroid = map.toCoordinate(pinch.centroid.position, false)
            }
            onScaleChanged: (delta) => {
                map.zoomLevel += Math.log2(delta)
                map.alignCoordinateToPoint(map.startCentroid, pinch.centroid.position)
            }
            onRotationChanged: (delta) => {
                map.bearing -= delta
                map.alignCoordinateToPoint(map.startCentroid, pinch.centroid.position)
            }
        }
        WheelHandler {
            id: wheel

            acceptedDevices: Qt.platform.pluginName === "cocoa" || Qt.platform.pluginName === "wayland"
                             ? PointerDevice.Mouse | PointerDevice.TouchPad
                             : PointerDevice.Mouse
            rotationScale: 1 / 120
            property: "zoomLevel"
        }
        DragHandler {
            id: drag

            target: null
            onTranslationChanged: (delta) => map.pan(-delta.x, -delta.y)
            onActiveChanged: if (active) {
                // 使用者手動拖曳：暫停自動置中，直到選取目標位置改變
                root.lastAutoCenterKey = root.bridge.selectedAgent + ":manual"
            }
        }
    }

    // 右下角：放大、縮小、朝北
    Column {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 16
        spacing: 6

        MapButton { text: "+"; tooltip: qsTr("放大"); onClicked: map.zoomLevel = Math.min(map.maximumZoomLevel, map.zoomLevel + 1) }
        MapButton { text: "−"; tooltip: qsTr("縮小"); onClicked: map.zoomLevel = Math.max(map.minimumZoomLevel, map.zoomLevel - 1) }
        MapButton {
            tooltip: qsTr("朝北")
            onClicked: map.bearing = 0
            contentItem: Text {
                text: "▲"
                rotation: -map.bearing
                color: Theme.danger
                font.pixelSize: 18
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    // 左下角：OSM 必要來源標示
    Rectangle {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        width: attribution.implicitWidth + 12
        height: attribution.implicitHeight + 6
        color: Qt.rgba(1, 1, 1, 0.75)

        Text {
            id: attribution

            anchors.centerIn: parent
            text: "© <a href=\"https://www.openstreetmap.org/copyright\">OpenStreetMap contributors</a>"
            textFormat: Text.RichText
            font.pixelSize: 11
            color: "#333333"
            onLinkActivated: (link) => Qt.openUrlExternally(link)
        }
    }

    component MapButton: ActionButton {
        textAlignment: Text.AlignHCenter
        implicitWidth: 40
        implicitHeight: 40
        padding: 0
        font.pixelSize: 20
        accentColor: Theme.textBody
    }
}
