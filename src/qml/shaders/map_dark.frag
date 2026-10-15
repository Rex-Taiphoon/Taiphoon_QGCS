// @file map_dark.frag
// @brief 地圖底圖深色化：灰階後反相、調暗並提高對比，讓 OSM 淺色圖磚變成近黑底（航電座艙風格）
// @author Taiphoon GCS Team
// @date 2026-10-15
//
// 等同 CSS filter: grayscale(1) invert(0.92) brightness(0.72) contrast(1.12)。
// 輸入為 premultiplied alpha，先還原再運算，輸出時重新乘回 alpha。
#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
};

layout(binding = 1) uniform sampler2D source;

const float INVERT_AMOUNT = 0.92;
const float BRIGHTNESS = 0.72;
const float CONTRAST = 1.12;

void main()
{
    vec4 color = texture(source, qt_TexCoord0);
    vec3 rgb = color.a > 0.0 ? color.rgb / color.a : vec3(0.0);
    float luma = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
    float value = mix(luma, 1.0 - luma, INVERT_AMOUNT) * BRIGHTNESS;
    value = clamp((value - 0.5) * CONTRAST + 0.5, 0.0, 1.0);
    fragColor = vec4(vec3(value) * color.a, color.a) * qt_Opacity;
}
