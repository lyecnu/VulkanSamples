#version 450

layout (location = 0) in vec3 inPosition;
layout (location = 1) in vec2 inUV;

layout (binding = 0) uniform UBO 
{
    mat4 projection;
    mat4 view;
    float lodBias;
} ubo;

layout (location = 0) out vec2 outUV;
layout (location = 1) out float outLodBias;

void main()
{
    outUV = inUV;
    outLodBias = ubo.lodBias;
	gl_Position = ubo.projection * ubo.view * vec4(inPosition, 1.0);
}