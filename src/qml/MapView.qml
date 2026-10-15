/**
 * @file MapView.qml
 * @brief 全幅地圖：去飽和 OSM 底圖、透明疊圖層上的航向箭頭與名稱、EGO-Swarm 軌跡、自動跟隨（ADR-0016）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Effects
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
    /// 已建立的機體標記（委派自行註冊），供名稱標籤避讓計算
    property var markers: []

    function registerMarker(marker) {
        markers.push(marker)
        root.scheduleDeclutter()
    }

    function unregisterMarker(marker) {
        const index = markers.indexOf(marker)
        if (index >= 0) {
            markers.splice(index, 1)
        }
        root.scheduleDeclutter()
    }

    /**
     * @brief 排程標籤避讓計算；計時中不重設，避免高頻遙測更新使計算永遠延後
     */
    function scheduleDeclutter() {
        if (!declutterTimer.running) {
            declutterTimer.start()
        }
    }

    /**
     * @brief 名稱標籤避讓：選取機體優先、其餘依名稱排序，與已顯示標籤重疊者隱藏（箭頭與提示仍保留）
     */
    function declutterLabels() {
        const placed = []
        const ordered = markers.filter(m => m.visible)
                               .sort((a, b) => (b.isSelected - a.isSelected) || a.agent.localeCompare(b.agent))
        for (const marker of ordered) {
            const point = overlay.fromCoordinate(marker.coordinate, false)
            const rect = {
                x: point.x - marker.labelWidth / 2,
                y: point.y + marker.labelOffset,
                w: marker.labelWidth,
                h: marker.labelHeight
            }
            const isOverlapping = placed.some(r => rect.x < r.x + r.w && r.x < rect.x + rect.w
                                                   && rect.y < r.y + r.h && r.y < rect.y + rect.h)
            marker.isLabelHidden = isOverlapping && !marker.isSelected
            if (!marker.isLabelHidden) {
                placed.push(rect)
            }
        }
    }

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
        function onTelemetryChanged() {
            root.followSelected()
            root.scheduleDeclutter()
        }
        function onSelectionChanged() {
            root.followSelected()
            root.scheduleDeclutter()
        }
    }

    // 視角或位置改變後最多每 150 ms 計算一次，避免拖曳與遙測更新時頻繁重算
    Timer {
        id: declutterTimer

        interval: 150
        onTriggered: root.declutterLabels()
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

    // 底圖：只有圖磚，去飽和並調暗，讓無人機與航跡成為畫面焦點
    Map {
        id: map

        property var startCentroid

        anchors.fill: parent
        plugin: osmPlugin
        center: root.defaultCenter
        zoomLevel: root.defaultZoom
        copyrightsVisible: false
        color: Theme.background
        layer.enabled: true
        layer.effect: MultiEffect {
            saturation: -1.0
            brightness: -0.7
            contrast: 0.35
        }

        Component.onCompleted: {
            // 「Custom URL Map」為自訂圖磚來源，位於清單最後
            for (let i = 0; i < supportedMapTypes.length; ++i) {
                if (supportedMapTypes[i].style === MapType.CustomMap) {
                    activeMapType = supportedMapTypes[i]
                }
            }
        }
    }

    // 疊圖層：透明的 itemsoverlay 地圖，與底圖同步視角，承載標記、航跡與手勢；不受底圖調色影響
    Map {
        id: overlay

        property var startCentroid

        anchors.fill: parent
        plugin: Plugin { name: "itemsoverlay" }
        color: "transparent"
        copyrightsVisible: false
        center: map.center
        zoomLevel: map.zoomLevel
        bearing: map.bearing
        tilt: map.tilt
        fieldOfView: map.fieldOfView
        minimumZoomLevel: map.minimumZoomLevel
        maximumZoomLevel: map.maximumZoomLevel
        minimumTilt: map.minimumTilt
        maximumTilt: map.maximumTilt
        minimumFieldOfView: map.minimumFieldOfView
        maximumFieldOfView: map.maximumFieldOfView

        onCenterChanged: root.scheduleDeclutter()
        onZoomLevelChanged: root.scheduleDeclutter()
        onBearingChanged: root.scheduleDeclutter()
        onWidthChanged: root.scheduleDeclutter()
        onHeightChanged: root.scheduleDeclutter()

        // 軌跡在標記下方
        MapItemView {
            model: root.bridge.trajectories
            delegate: MapPolyline {
                required property var modelData

                line.width: modelData.isSelected ? 2.5 : 1.5
                line.color: modelData.isSelected ? Theme.trajectorySelected : Theme.trajectoryOther
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
                readonly property int arrowSize: isSelected ? 30 : 26
                /// 標籤避讓結果（由 MapView.declutterLabels 設定）
                property bool isLabelHidden: false
                readonly property real labelWidth: nameLabel.width
                readonly property real labelHeight: nameLabel.height
                /// 標籤上緣相對座標點的位移（anchorPoint 位於箭頭中心）
                readonly property real labelOffset: arrowSize / 2 + 10

                visible: hasPosition
                // 選中的標記畫在最上層，避免與鄰近無人機重疊時被遮住
                z: isSelected ? 1 : 0
                coordinate: QtPositioning.coordinate(latitude, longitude)
                anchorPoint.x: sourceItem.width / 2
                anchorPoint.y: arrowSize / 2
                zoomLevel: 0

                Component.onCompleted: root.registerMarker(marker)
                Component.onDestruction: root.unregisterMarker(marker)

                sourceItem: Item {
                    width: Math.max(marker.arrowSize + 16, nameLabel.width)
                    height: marker.arrowSize + 10 + nameLabel.height

                    // 選取機體外圈
                    Rectangle {
                        visible: marker.isSelected
                        x: (parent.width - width) / 2
                        y: (marker.arrowSize - height) / 2
                        width: marker.arrowSize + 16
                        height: width
                        radius: width / 2
                        color: "transparent"
                        border.color: Theme.markerSelected
                        border.width: 1.5
                    }

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
                            strokeColor: Theme.background
                            strokeWidth: 1.5
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

                        visible: !marker.isLabelHidden
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: marker.arrowSize + 10
                        width: nameText.implicitWidth + 10
                        height: nameText.implicitHeight + 4
                        radius: Theme.radius
                        color: Theme.panel
                        border.color: marker.isSelected ? Theme.markerSelected : Theme.border

                        Text {
                            id: nameText

                            anchors.centerIn: parent
                            text: marker.agent.replace(/_agent$/, "")
                            color: marker.isSelected ? Theme.markerSelected : Theme.textStrong
                            font.family: Theme.fontMono
                            font.pixelSize: 12
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
            // 縮放寫入底圖，疊圖層透過繫結跟隨
            target: map
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
        color: Theme.panel

        Text {
            id: attribution

            anchors.centerIn: parent
            text: "© <a href=\"https://www.openstreetmap.org/copyright\">OpenStreetMap contributors</a>"
            textFormat: Text.RichText
            font.pixelSize: 11
            color: Theme.textMuted
            linkColor: Theme.textBody
            onLinkActivated: (link) => Qt.openUrlExternally(link)
        }
    }

    component MapButton: ActionButton {
        textAlignment: Text.AlignHCenter
        implicitWidth: 36
        implicitHeight: 36
        padding: 0
        font.pixelSize: 20
        accentColor: Theme.commandNormal
    }
}
