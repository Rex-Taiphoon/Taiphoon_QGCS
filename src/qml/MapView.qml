/**
 * @file MapView.qml
 * @brief 全幅地圖：反相深色 OSM 底圖、透明疊圖層上的航向箭頭與名稱、EGO-Swarm 軌跡、自動跟隨（ADR-0016）
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
    /// 縮放上限：OSM 圖磚只到 19 級，之後放大既有圖磚；兩層地圖共用此上限，
    /// 否則疊圖層停在 19 級而底圖繼續放大，機體間距不再隨縮放改變且與底圖錯位
    readonly property real maximumZoom: 22

    /**
     * @brief 將縮放層級限制在可用範圍
     * @param zoom 欲設定的層級
     * @return 限制後的層級
     */
    function clampZoom(zoom) {
        return Math.max(map.minimumZoomLevel, Math.min(maximumZoom, zoom))
    }
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

    // 底圖：只有圖磚，以 shader 灰階反相成近黑底，讓無人機、航跡與面板成為畫面焦點
    Map {
        id: map

        property var startCentroid

        anchors.fill: parent
        plugin: osmPlugin
        center: root.defaultCenter
        zoomLevel: root.defaultZoom
        copyrightsVisible: false
        // 滾輪直接寫入 zoomLevel 不受圖磚上限限制，在此統一夾住
        onZoomLevelChanged: if (zoomLevel !== root.clampZoom(zoomLevel)) {
            zoomLevel = root.clampZoom(zoomLevel)
        }
        // 背景色經 shader 反相後才是畫面上的顏色：取 OSM 陸地色，圖磚載入前就呈現與底圖一致的深色
        color: "#f2efe9"
        layer.enabled: true
        layer.effect: ShaderEffect {
            fragmentShader: "qrc:/qt/qml/TaiphoonQgcs/shaders/map_dark.frag.qsb"
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
        maximumZoomLevel: root.maximumZoom
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
                required property string altitudeText
                required property bool isWaitingForFlightController

                readonly property color markerColor: isSelected ? Theme.markerSelected : Theme.markerNormal
                readonly property string shortName: agent.replace(/_agent$/, "").toUpperCase()
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

                        // 等待飛控時以琥珀色空心箭頭表示（注意）
                        ShapePath {
                            fillColor: marker.isWaitingForFlightController ? "transparent" : marker.markerColor
                            strokeColor: marker.isWaitingForFlightController ? Theme.caution : Theme.background
                            strokeWidth: 1.5
                            startX: 0
                            startY: 0
                            PathLine { x: arrow.width; y: arrow.height / 2 }
                            PathLine { x: 0; y: arrow.height }
                            PathLine { x: arrow.width * 0.28; y: arrow.height / 2 }
                            PathLine { x: 0; y: 0 }
                        }
                    }

                    // 名稱標籤：無框文字＋深色描邊，機號、模式（狀態色）與高度
                    Text {
                        id: nameLabel

                        visible: !marker.isLabelHidden
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: marker.arrowSize + 10
                        text: marker.isWaitingForFlightController
                              ? marker.shortName + " <font color=\"" + Theme.caution + "\">" + qsTr("等待飛控")
                                + "</font>"
                              : marker.shortName + " <font color=\"" + Theme.normal + "\">" + marker.modeLabel
                                + "</font> " + marker.altitudeText
                        textFormat: Text.StyledText
                        color: marker.isSelected ? Theme.markerSelected : Theme.textStrong
                        style: Text.Outline
                        styleColor: Theme.background
                        font.family: Theme.fontMono
                        font.pixelSize: 12
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
                map.zoomLevel = root.clampZoom(map.zoomLevel + Math.log2(delta))
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

    // 右下角：放大、縮小、朝北，合併為一組直角按鈕
    Rectangle {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spaceLg
        width: zoomColumn.width + 2
        height: zoomColumn.height + 2
        color: Theme.panel
        border.color: Theme.border

        Column {
            id: zoomColumn

            x: 1
            y: 1

            MapButton {
                text: "+"
                tooltip: qsTr("放大")
                onClicked: map.zoomLevel = root.clampZoom(map.zoomLevel + 1)
            }
            Rectangle { width: parent.width; height: 1; color: Theme.borderSoft }
            MapButton {
                text: "−"
                tooltip: qsTr("縮小")
                onClicked: map.zoomLevel = root.clampZoom(map.zoomLevel - 1)
            }
            Rectangle { width: parent.width; height: 1; color: Theme.borderSoft }
            MapButton {
                tooltip: qsTr("朝北")
                onClicked: map.bearing = 0
                contentItem: Text {
                    text: "N"
                    rotation: -map.bearing
                    color: map.bearing === 0 ? Theme.textBody : Theme.accent
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontLabel + 1
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    // 左下角：地圖中心座標、縮放層級與 OSM 必要來源標示
    Rectangle {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        width: footer.implicitWidth + 2 * Theme.spaceSm
        height: footer.implicitHeight + Theme.spaceXs
        color: Theme.panel

        Row {
            id: footer

            anchors.centerIn: parent
            spacing: Theme.spaceLg

            FooterText {
                text: Math.abs(map.center.latitude).toFixed(5) + (map.center.latitude >= 0 ? " N  " : " S  ")
                      + Math.abs(map.center.longitude).toFixed(5) + (map.center.longitude >= 0 ? " E" : " W")
            }
            FooterText { text: "Z" + Math.round(map.zoomLevel) }
            FooterText {
                text: "© <a href=\"https://www.openstreetmap.org/copyright\">OpenStreetMap contributors</a>"
                textFormat: Text.RichText
                font.family: Theme.fontFamily
                linkColor: Theme.textBody
                onLinkActivated: (link) => Qt.openUrlExternally(link)
            }
        }
    }

    component FooterText: Text {
        color: Theme.textMuted
        font.family: Theme.fontMono
        font.pixelSize: Theme.fontLabel
    }

    component MapButton: ActionButton {
        textAlignment: Text.AlignHCenter
        implicitWidth: 34
        implicitHeight: 34
        padding: 0
        font.family: Theme.fontMono
        font.pixelSize: 16
        accentColor: Theme.commandNormal
        background: Rectangle {
            color: parent.down ? Theme.controlActive : (parent.hovered ? Theme.controlHover : "transparent")
        }
    }
}
