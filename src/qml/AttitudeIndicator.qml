/**
 * @file AttitudeIndicator.qml
 * @brief 圓形姿態儀：天空／地面隨 roll、pitch 轉動，頂端黃色三角指示 roll（網頁版 AttitudeIndicator）
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
import QtQuick

Item {
    id: root

    /// roll（度）
    property real roll: 0
    /// pitch（度）
    property real pitch: 0

    implicitWidth: 72
    implicitHeight: 72

    onRollChanged: canvas.requestPaint()
    onPitchChanged: canvas.requestPaint()

    Canvas {
        id: canvas

        anchors.fill: parent
        antialiasing: true

        onPaint: {
            const ctx = getContext("2d")
            const size = Math.min(width, height)
            const half = size / 2
            const radius = half - 4
            // pitch 90 度對應半徑，超出時夾在圓內
            const pitchOffset = Math.max(-radius, Math.min(radius, (root.pitch / 90) * radius))
            const horizonY = half - pitchOffset
            ctx.reset()

            ctx.beginPath()
            ctx.arc(half, half, radius, 0, Math.PI * 2)
            ctx.fillStyle = "#111111"
            ctx.fill()

            ctx.save()
            ctx.beginPath()
            ctx.arc(half, half, radius - 1, 0, Math.PI * 2)
            ctx.clip()
            ctx.translate(half, half)
            ctx.rotate(-root.roll * Math.PI / 180)
            ctx.translate(-half, -half)
            ctx.fillStyle = "#1a3a6a"
            ctx.fillRect(-size, -size, size * 3, horizonY + size)
            ctx.fillStyle = "#4a2a1a"
            ctx.fillRect(-size, horizonY, size * 3, size * 2)
            ctx.strokeStyle = "rgba(255, 255, 255, 0.6)"
            ctx.lineWidth = 1
            ctx.beginPath()
            ctx.moveTo(-size, horizonY)
            ctx.lineTo(size * 2, horizonY)
            ctx.stroke()
            ctx.restore()

            ctx.strokeStyle = "#444444"
            ctx.lineWidth = 1.5
            ctx.beginPath()
            ctx.arc(half, half, radius, 0, Math.PI * 2)
            ctx.stroke()

            ctx.strokeStyle = "#888888"
            ctx.lineWidth = 0.5
            ctx.beginPath()
            ctx.moveTo(4, half)
            ctx.lineTo(size - 4, half)
            ctx.moveTo(half, 4)
            ctx.lineTo(half, size - 4)
            ctx.stroke()

            ctx.fillStyle = "#00ff00"
            ctx.beginPath()
            ctx.arc(half, half, 2, 0, Math.PI * 2)
            ctx.fill()

            ctx.save()
            ctx.translate(half, half)
            ctx.rotate(root.roll * Math.PI / 180)
            ctx.translate(-half, -half)
            ctx.fillStyle = "#ffff00"
            ctx.beginPath()
            ctx.moveTo(half, 2)
            ctx.lineTo(half - 4, 8)
            ctx.lineTo(half + 4, 8)
            ctx.closePath()
            ctx.fill()
            ctx.restore()
        }
    }
}
