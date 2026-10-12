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

    /// 圖示名稱：close、settings、minus、chevronDown、power、hexagon
    property string name: "close"
    property color color: Theme.textMuted
    property real lineWidth: 2

    readonly property var paths: ({
        "close": "M18 6 L6 18 M6 6 L18 18",
        "minus": "M5 12 L19 12",
        "chevronDown": "M6 9 L12 15 L18 9",
        "power": "M12 2 L12 12 M18.36 6.64 A9 9 0 1 1 5.64 6.64",
        "hexagon": "M21 16 L21 8 L12 3 L3 8 L3 16 L12 21 Z",
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
