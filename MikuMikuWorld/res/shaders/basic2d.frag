#version 330 core

in vec3 uv1;
in vec4 color;

out vec4 fragColor;

struct Material
{
    sampler2D diffuse0;
    sampler2D diffuse1;
    float blend;
};

uniform Material material;
uniform int blendMode;

void main()
{
    vec4 texColor = texture(material.diffuse0, uv1.xy / uv1.z) * color;
    fragColor = texColor;
}
