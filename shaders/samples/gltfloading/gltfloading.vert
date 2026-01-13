#version 450

layout (location = 0) in vec3 inPosition;
layout (location = 1) in vec2 inUV;
layout (location = 2) in vec3 inColor;

layout (binding = 0) uniform UBO
{
    mat4 projection;
    mat4 view;
} ubo;

layout (push_constant) uniform PushConsts
{
    mat4 model;
} pushConsts;

layout (location = 0) out vec2 outUV;
layout (location = 1) out vec3 outColor;

void main()
{
    outUV = inUV;
    outColor = inColor;
	gl_Position = ubo.projection * ubo.view * pushConsts.model * vec4(inPosition, 1.0);
}