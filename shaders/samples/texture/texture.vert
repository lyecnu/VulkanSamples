#version 450

layout (location = 0) in vec3 inPosition;
layout (location = 1) in vec3 inColor;

layout (binding = 0) uniform UBO 
{
    mat4 projection;
    mat4 view;
    mat4 model;
} ubo;

layout (location = 0) out vec3 outColor;

void main()
{
    outColor = inColor;
	gl_Position = ubo.projection * ubo.view * ubo.model * vec4(inPosition, 1.0);
}