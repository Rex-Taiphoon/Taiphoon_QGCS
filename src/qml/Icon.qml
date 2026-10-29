/**
 * @file Icon.qml
 * @brief 向量線條圖示（24×24 座標），不依賴平台字型是否含有符號字元
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
import QtQuick
import QtQuick.Shapes

Item {
    id: root

    /// 圖示名稱，見下方 paths（命令、方向、編隊與通用圖示）
    property string name: "close"
    property color color: Theme.textMuted
    property real lineWidth: 2

    readonly property var paths: ({
        "close": "M18 6 L6 18 M6 6 L18 18",
        "minus": "M5 12 L19 12",
        "plus": "M12 5 L12 19 M5 12 L19 12",
        "chevronDown": "M6 9 L12 15 L18 9",
        "chevronUp": "M6 15 L12 9 L18 15",
        "send": "M22 2 L15 22 L11 13 L2 9 Z M22 2 L11 13",
        "lock": "M5 11 L19 11 L19 21 L5 21 Z M8 11 L8 8 A4 4 0 0 1 16 8 L16 11",
        "unlock": "M5 11 L19 11 L19 21 L5 21 Z M8 11 L8 8 A4 4 0 0 1 15.5 6",
        "offboard": "M12 10 L12 14 M8.5 15.5 A5 5 0 0 1 8.5 8.5 M15.5 8.5 A5 5 0 0 1 15.5 15.5"
                    + " M5.6 18.4 A9 9 0 0 1 5.6 5.6 M18.4 5.6 A9 9 0 0 1 18.4 18.4",
        "takeoff": "M12 18 L12 5 M6 11 L12 5 L18 11 M5 21 L19 21",
        "land": "M12 3 L12 16 M6 10 L12 16 L18 10 M5 21 L19 21",
        "rtl": "M3 11 L12 4 L21 11 M6 10 L6 20 L18 20 L18 10 M10 20 L10 15 L14 15 L14 20",
        "arrowUp": "M12 19 L12 5 M6 11 L12 5 L18 11",
        "arrowDown": "M12 5 L12 19 M6 13 L12 19 L18 13",
        "arrowLeft": "M19 12 L5 12 M11 6 L5 12 L11 18",
        "arrowRight": "M5 12 L19 12 M13 6 L19 12 L13 18",
        "yaw": "M20 12 A8 8 0 1 1 17.7 6.3 M20 3 L20 8 L15 8",
        "formCircle": "M5 12 A7 7 0 1 0 19 12 A7 7 0 1 0 5 12 M12 4 L12 6 M5.5 16 L7 15 M18.5 16 L17 15",
        "formLine": "M3 12 L5 12 M11 12 L13 12 M19 12 L21 12 M7 12 L9 12 M15 12 L17 12",
        "formVertical": "M12 3 L12 5 M12 11 L12 13 M12 19 L12 21 M12 7 L12 9 M12 15 L12 17",
        "check": "M5 12 L10 17 L19 7",
        "alert": "M12 3 L22 20 L2 20 Z M12 9 L12 14 M12 17 L12 17.5",
        "power": "M12 2 L12 12 M18.36 6.64 A9 9 0 1 1 5.64 6.64",
        "hexagon": "M21 16 L21 8 L12 3 L3 8 L3 16 L12 21 Z",
        "video": "M3 7 L15 7 L15 17 L3 17 Z M15 10.5 L21 7 L21 17 L15 13.5",
        "popOut": "M14 4 L20 4 L20 10 M20 4 L11 13 M17 14 L17 20 L4 20 L4 7 L10 7",
        "fullscreen": "M4 9 L4 4 L9 4 M15 4 L20 4 L20 9 M20 15 L20 20 L15 20 M9 20 L4 20 L4 15",
        "settings": "M12.22 2h-.44a2 2 0 0 0-2 2v.18a2 2 0 0 1-1 1.73l-.43.25a2 2 0 0 1-2 0l-.15-.08"
                    + "a2 2 0 0 0-2.73.73l-.22.38a2 2 0 0 0 .73 2.73l.15.1a2 2 0 0 1 1 1.72v.51a2 2 0 0 1-1 1.74"
                    + "l-.15.09a2 2 0 0 0-.73 2.73l.22.38a2 2 0 0 0 2.73.73l.15-.08a2 2 0 0 1 2 0l.43.25"
                    + "a2 2 0 0 1 1 1.73V20a2 2 0 0 0 2 2h.44a2 2 0 0 0 2-2v-.18a2 2 0 0 1 1-1.73l.43-.25"
                    + "a2 2 0 0 1 2 0l.15.08a2 2 0 0 0 2.73-.73l.22-.39a2 2 0 0 0-.73-2.73l-.15-.08"
                    + "a2 2 0 0 1-1-1.74v-.5a2 2 0 0 1 1-1.74l.15-.09a2 2 0 0 0 .73-2.73l-.22-.38"
                    + "a2 2 0 0 0-2.73-.73l-.15.08a2 2 0 0 1-2 0l-.43-.25a2 2 0 0 1-1-1.73V4a2 2 0 0 0-2-2z"
                    + "M15 12a3 3 0 1 1-6 0a3 3 0 0 1 6 0z"
    })

    implicitWidth: 22
    implicitHeight: 22

    Shape {
        anchors.centerIn: parent
        width: 24
        height: 24
        scale: Math.min(root.width, root.height) / 24
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: root.color
            strokeWidth: root.lineWidth
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: root.paths[root.name] || "" }
        }
    }
}
