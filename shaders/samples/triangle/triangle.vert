#version 450

layout (location = 0) in vec3 inPosition;
layout (location = 1) in vec3 inColor;

layout (binding = 0) uniform MVP
{
	mat4 projection;
	mat4 view;
} mvp;

layout (location = 0) out vec3 fragColor;

void main()
{
	fragColor = inColor;
	gl_Position = mvp.projection * mvp.view * vec4(inPosition, 1.0);
}