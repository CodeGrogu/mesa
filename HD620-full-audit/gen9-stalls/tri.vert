#version 450
layout(location = 0) in vec2 pos;
layout(push_constant) uniform P { vec4 offset; };
void main() { gl_Position = vec4(pos + offset.xy, 0.0, 1.0); }
