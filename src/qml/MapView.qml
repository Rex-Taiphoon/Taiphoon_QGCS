/**
 * @file MapView.qml
 * @brief 全幅地圖：反相深色 OSM 底圖、透明疊圖層上的航向箭頭與名稱、EGO-Swarm 軌跡、航點任務路線與點選新增、
 *        地理圍籬顯示與編輯、自動跟隨（ADR-0016）
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
    required property var mission
    required property var geofence

    /// 尚未取得有效 GPS 時的預設中心（台北），與網頁版相同
    readonly property var defaultCenter: QtPositioning.coordinate(25.03997, 121.51195)
    readonly property real defaultZoom: 16
    /// 縮放上限：OSM 圖磚只到 19 級，之後放大既有圖磚；兩層地圖共用此上限，
    /// 否則疊圖層停在上限而底圖繼續放大，機體間距不再隨縮放改變且與底圖錯位。
    /// 24 級約 1 公尺 118 像素（緯度 25 度），近距離編隊仍能分辨各機與航點
    readonly property real maximumZoom: 24
    /// 是否顯示航點與路線：進階設定可關閉，編輯航點時一律顯示，否則看不到剛點下的位置
    readonly property bool isWaypointVisible: settings.showWaypoints || mission.isEditing
    /// 拖曳中的圍籬頂點與暫時位置；放開時才寫回 GeofenceController，拖曳期間模型不重建，委派不會被中斷
    property int draggingVertex: -1
    property var draggingCoordinate: null
    /// 圍籬草稿的路徑（含拖曳中的暫時位置）
    readonly property var draftCoordinates: geofence.draftPath.map(
        (p, i) => (i === draggingVertex && draggingCoordinate !== null)
                  ? draggingCoordinate : QtPositioning.coordinate(p.latitude, p.longitude))
    readonly property color draftColor: geofence.isDraftSelfIntersecting ? Theme.danger : Theme.accent

    /**
     * @brief 點選位置是否壓在圍籬草稿的頂點上（點頂點不新增頂點）
     * @param position 疊圖層座標
     * @return 是否在頂點附近
     */
    function isNearDraftVertex(position) {
        for (const coordinate of draftCoordinates) {
            const point = overlay.fromCoordinate(coordinate, false)
            if (Math.hypot(point.x - position.x, point.y - position.y) < 12) {
                return true
            }
        }
        return false
    }

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
        // 滾輪直接寫入 zoomLevel 不受圖磚上限限制，在此統一夾住。必須寫 map.zoomLevel：
        // 處理器內的 zoomLevel 是訊號參數，指派給它不會改到屬性，底圖會繼續放大而與疊圖層錯位
        onZoomLevelChanged: if (map.zoomLevel !== root.clampZoom(map.zoomLevel)) {
            map.zoomLevel = root.clampZoom(map.zoomLevel)
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

        // 生效中的圍籬：琥珀色實線加淡填色；編輯時淡化，讓草稿成為焦點
        MapPolygon {
            visible: root.geofence.hasFence
            opacity: root.geofence.isEditing ? 0.35 : 1
            border.width: 2
            border.color: Theme.caution
            color: Qt.alpha(Theme.caution, 0.06)
            path: root.geofence.fencePath.map(p => QtPositioning.coordinate(p.latitude, p.longitude))
        }
        // margin：向內縮的虛線（無法內縮時不顯示）
        MapItemView {
            model: root.geofence.hasFence && !root.geofence.isEditing ? root.geofence.fenceInsetDashes : []
            delegate: MapPolyline {
                required property var modelData

                line.width: 1
                line.color: Qt.alpha(Theme.caution, 0.8)
                path: modelData.path.map(p => QtPositioning.coordinate(p.latitude, p.longitude))
            }
        }
        // 編輯中的草稿：青色；邊互相交叉時改為紅色
        MapPolygon {
            visible: root.geofence.isEditing && root.draftCoordinates.length >= 3
            border.width: 2
            border.color: root.draftColor
            color: Qt.alpha(root.draftColor, 0.08)
            path: root.draftCoordinates
        }
        MapPolyline {
            visible: root.geofence.isEditing && root.draftCoordinates.length === 2
            line.width: 2
            line.color: root.draftColor
            path: root.draftCoordinates
        }

        // 航點任務路線：選取機為青色，其他機淡化；位於無人機標記下方。
        // 進階設定可隱藏航點，編輯中一律顯示；以空模型隱藏，MapItemView 的 visible 不一定套用到地圖項目
        MapItemView {
            model: root.isWaypointVisible ? root.mission.routes : []
            delegate: MapPolyline {
                required property var modelData

                line.width: modelData.isSelected ? 2 : 1
                line.color: modelData.isSelected ? Theme.accent : Qt.alpha(Theme.accent, 0.35)
                path: modelData.points.map(p => QtPositioning.coordinate(p.latitude, p.longitude))
            }
        }

        // 航點編號；目前要前往的航點以實心標示
        MapItemView {
            model: root.isWaypointVisible ? root.mission.waypointMarkers : []
            delegate: MapQuickItem {
                id: waypointMarker

                required property var modelData

                coordinate: QtPositioning.coordinate(modelData.latitude, modelData.longitude)
                anchorPoint.x: sourceItem.width / 2
                anchorPoint.y: sourceItem.height / 2
                opacity: modelData.isSelected ? 1 : 0.45

                sourceItem: Rectangle {
                    width: 20
                    height: 20
                    radius: width / 2
                    color: waypointMarker.modelData.isCurrent ? Theme.accent : Theme.background
                    border.color: Theme.accent
                    border.width: 1.5

                    Text {
                        anchors.centerIn: parent
                        text: waypointMarker.modelData.index + 1
                        color: waypointMarker.modelData.isCurrent ? Theme.background : Theme.accent
                        font.family: Theme.fontMono
                        font.pixelSize: 11
                    }
                }
            }
        }

        // 圍籬草稿頂點：拖曳移動、右鍵刪除
        MapItemView {
            model: root.geofence.isEditing ? root.geofence.draftPoints : []
            delegate: MapQuickItem {
                id: vertexMarker

                required property var modelData

                z: 2
                coordinate: (root.draggingVertex === modelData.index && root.draggingCoordinate !== null)
                            ? root.draggingCoordinate
                            : QtPositioning.coordinate(modelData.latitude, modelData.longitude)
                anchorPoint.x: sourceItem.width / 2
                anchorPoint.y: sourceItem.height / 2

                sourceItem: Rectangle {
                    id: vertexBox

                    width: 16
                    height: 16
                    color: vertexDrag.active || vertexHover.hovered ? root.draftColor : Theme.background
                    border.color: root.draftColor
                    border.width: 1.5

                    Text {
                        anchors.centerIn: parent
                        text: vertexMarker.modelData.index + 1
                        color: vertexDrag.active || vertexHover.hovered ? Theme.background : root.draftColor
                        font.family: Theme.fontMono
                        font.pixelSize: 9
                    }

                    HoverHandler {
                        id: vertexHover

                        cursorShape: Qt.SizeAllCursor
                    }
                    ToolTip.visible: vertexHover.hovered && !vertexDrag.active
                    ToolTip.delay: 600
                    ToolTip.text: qsTr("拖曳移動，右鍵刪除")
                    DragHandler {
                        id: vertexDrag

                        target: null
                        onActiveChanged: {
                            if (active) {
                                root.draggingVertex = vertexMarker.modelData.index
                                return
                            }
                            const index = root.draggingVertex
                            const coordinate = root.draggingCoordinate
                            root.draggingVertex = -1
                            root.draggingCoordinate = null
                            if (coordinate !== null) {
                                root.geofence.moveVertex(index, coordinate.latitude, coordinate.longitude)
                            }
                        }
                        onCentroidChanged: if (active) {
                            const point = vertexBox.mapToItem(overlay, centroid.position.x, centroid.position.y)
                            root.draggingCoordinate = overlay.toCoordinate(point, false)
                        }
                    }
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: root.geofence.removeVertex(vertexMarker.modelData.index)
                    }
                }
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
        // 航點編輯：點選地圖新增航點（拖曳仍為平移），游標改為十字；圍籬編輯中優先新增圍籬頂點
        TapHandler {
            enabled: root.mission.isEditing && !root.geofence.isEditing
            onTapped: (eventPoint, button) => {
                const coordinate = overlay.toCoordinate(eventPoint.position, false)
                root.mission.addWaypoint(coordinate.latitude, coordinate.longitude)
            }
        }
        // 圍籬編輯：點選地圖在最後一個頂點之後新增頂點；點在既有頂點上不新增
        TapHandler {
            enabled: root.geofence.isEditing
            onTapped: (eventPoint, button) => {
                if (root.isNearDraftVertex(eventPoint.position)) {
                    return
                }
                const coordinate = overlay.toCoordinate(eventPoint.position, false)
                root.geofence.addVertex(coordinate.latitude, coordinate.longitude)
            }
        }
        HoverHandler {
            enabled: root.mission.isEditing || root.geofence.isEditing
            cursorShape: Qt.CrossCursor
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

    // 上方中央：圍籬警示（各機版本不一致、有飛機在圍籬外）
    Rectangle {
        visible: root.geofence.mapWarningText.length > 0
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Theme.spaceLg
        width: Math.min(fenceWarning.implicitWidth + 2 * Theme.spaceMd, parent.width - 2 * Theme.spaceLg)
        height: fenceWarning.implicitHeight + Theme.spaceSm
        color: Theme.cautionInset
        border.color: Theme.caution

        Label {
            id: fenceWarning

            anchors.centerIn: parent
            width: Math.min(implicitWidth, parent.width - 2 * Theme.spaceMd)
            text: qsTr("圍籬：") + root.geofence.mapWarningText
            color: Theme.caution
            elide: Text.ElideRight
            font.pixelSize: Theme.fontSmall
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
