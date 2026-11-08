#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec4 aColor;
layout (location = 2) in vec4 aUV1;	// u, v, (unused), perspective weight (0 = flat)

out vec3 uv1;	// u * w, v * w, w: divided per pixel, like a 3D quad
out vec4 color;

uniform mat4 view;
uniform mat4 projection;

void main()
{
    float w     = aUV1.w > 0.0 ? aUV1.w : 1.0;
    uv1         = vec3(aUV1.xy * w, w);
    color       = aColor;
    gl_Position = projection * vec4(aPos, 1.0);
}