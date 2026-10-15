/**
 * @file AttitudeIndicator.qml
 * @brief 圓形姿態儀（Airbus PFD 慣例）：天空／地面、俯仰刻度與滾轉刻度隨姿態移動，琥珀色符號固定代表機身
 * @author Taiphoon GCS Team
 * @date 2026-10-15
 *
 * 角度慣例與 AgentAttitude 四元數（PX4 FRD → NED）一致：
 * roll 正值為右滾（地平線逆時針轉），pitch 正值為機頭上仰（地平線下移、天空變多）。
 * 多旋翼向前飛時機頭下俯，pitch 為負，地面區塊變多。
 * 琥珀色的機身符號與頂端三角形固定不動；三角形對準滾轉刻度的位置即為目前滾轉角。
 */
import QtQuick

Item {
    id: root

    /// roll（度，右滾為正）
    property real roll: 0
    /// pitch（度，機頭上仰為正）
    property real pitch: 0

    /// 半徑對應的俯仰角（度）；30 度讓懸停時的小角度變化也看得出來
    readonly property real pitchRangeDegrees: 30
    /// 俯仰刻度（度）
    readonly property var pitchMarks: [-20, -10, 10, 20]
    /// 滾轉刻度（度），0 度另畫三角形
    readonly property var rollMarks: [-60, -45, -30, -20, -10, 10, 20, 30, 45, 60]

    implicitWidth: 88
    implicitHeight: 88

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
            const radius = half - 2
            // 滾轉刻度在外圈，姿態球縮在內側
            const ballRadius = radius - 7
            const pixelsPerDegree = ballRadius / root.pitchRangeDegrees
            // 機頭上仰時地平線下移；超出時夾在球內，保留一點天空或地面
            const pitchOffset = Math.max(-ballRadius * 0.9,
                                         Math.min(ballRadius * 0.9, root.pitch * pixelsPerDegree))
            const rollRadians = root.roll * Math.PI / 180
            ctx.reset()

            // 滾轉刻度隨地平線旋轉；0 度位置畫空心三角形。
            // 刻度在球外，須在裁切前畫完（Canvas 的 restore() 不會解除 clip）
            ctx.save()
            ctx.translate(half, half)
            ctx.rotate(-rollRadians)
            ctx.strokeStyle = Theme.textStrong
            ctx.lineWidth = 1.5
            ctx.beginPath()
            for (let j = 0; j < root.rollMarks.length; ++j)
            {
                const angle = root.rollMarks[j] * Math.PI / 180
                const isMajor = Math.abs(root.rollMarks[j]) % 30 === 0
                const inner = ballRadius + 1
                const outer = ballRadius + (isMajor ? 6 : 4)
                ctx.moveTo(inner * Math.sin(angle), -inner * Math.cos(angle))
                ctx.lineTo(outer * Math.sin(angle), -outer * Math.cos(angle))
            }
            ctx.moveTo(0, -ballRadius - 1)
            ctx.lineTo(-3, -ballRadius - 6)
            ctx.lineTo(3, -ballRadius - 6)
            ctx.closePath()
            ctx.stroke()
            ctx.restore()

            // 姿態球：以中心旋轉 -roll 後再依 pitch 平移，刻度與地平線一起移動
            ctx.save()
            ctx.beginPath()
            ctx.arc(half, half, ballRadius, 0, Math.PI * 2)
            ctx.clip()
            ctx.translate(half, half)
            ctx.rotate(-rollRadians)
            ctx.translate(0, pitchOffset)
            ctx.fillStyle = Theme.sky
            ctx.fillRect(-size, -size * 2, size * 2, size * 2)
            ctx.fillStyle = Theme.ground
            ctx.fillRect(-size, 0, size * 2, size * 2)
            ctx.strokeStyle = Theme.textStrong
            ctx.lineWidth = 1
            ctx.beginPath()
            ctx.moveTo(-size, 0)
            ctx.lineTo(size, 0)
            for (let i = 0; i < root.pitchMarks.length; ++i)
            {
                const mark = root.pitchMarks[i]
                const markY = -mark * pixelsPerDegree
                const markHalf = Math.abs(mark) === 10 ? ballRadius * 0.28 : ballRadius * 0.18
                ctx.moveTo(-markHalf, markY)
                ctx.lineTo(markHalf, markY)
            }
            ctx.stroke()
            ctx.restore()

            ctx.strokeStyle = Theme.border
            ctx.lineWidth = 1
            ctx.beginPath()
            ctx.arc(half, half, ballRadius, 0, Math.PI * 2)
            ctx.stroke()

            // 固定的機身符號（琥珀）：頂端滾轉指標、左右機翼與中心點
            ctx.fillStyle = Theme.caution
            ctx.strokeStyle = Theme.background
            ctx.lineWidth = 1
            ctx.beginPath()
            ctx.moveTo(half, half - ballRadius + 1)
            ctx.lineTo(half - 4, half - ballRadius + 7)
            ctx.lineTo(half + 4, half - ballRadius + 7)
            ctx.closePath()
            ctx.fill()

            const wingInner = ballRadius * 0.22
            const wingOuter = ballRadius * 0.62
            ctx.beginPath()
            ctx.rect(half - wingOuter, half - 1.5, wingOuter - wingInner, 3)
            ctx.rect(half + wingInner, half - 1.5, wingOuter - wingInner, 3)
            ctx.rect(half - wingInner, half - 1.5, 3, 6)
            ctx.rect(half + wingInner - 3, half - 1.5, 3, 6)
            ctx.rect(half - 1.5, half - 1.5, 3, 3)
            ctx.fill()
            ctx.stroke()
        }
    }
}
