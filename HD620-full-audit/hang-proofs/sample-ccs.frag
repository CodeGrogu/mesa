#version 450
// Fragment sampling for the Gfx9 ASTC 5x5 + CCS check: ccs.
layout(binding = 0) uniform sampler2D ccs_tex;
layout(binding = 1) uniform sampler2D astc_tex;
layout(location = 0) out vec4 color;
vec2 uv(uint h) { return vec2(h & 0xffffu, h >> 16) / 65536.0; }
void main()
{
    uint h = uint(gl_FragCoord.x) * 1973u + uint(gl_FragCoord.y) * 9277u + 1u;
    vec4 c = vec4(0.0);
    for (int k = 0; k < 16; k++) {
        h = h * 1664525u + 1013904223u;
        c += textureLod(ccs_tex, uv(h), 0.0);
    }
    color = c * 0.01;
}
