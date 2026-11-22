#version 450
layout(location = 0) out vec4 color;
void main()
{
    vec2 p = gl_FragCoord.xy;
    color = vec4(fract(p.x / 37.0), fract(p.y / 23.0), fract((p.x + p.y) / 61.0), 1.0);
}
