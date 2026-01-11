#version 450

layout (location = 0) in vec3 inPosition;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec3 inColor;

layout (binding = 0) uniform UBO 
{
    mat4 projection;
    mat4 view;
    vec4 lightPos;
} ubo;

layout (location = 0) out vec3 viewPos;
layout (location = 1) out vec3 viewNormal;
layout (location = 2) out vec3 outColor;

void main()
{
    viewPos = (ubo.view * vec4(inPosition, 1.0)).xyz;
    viewNormal = mat3(ubo.view) * inNormal;
    outColor = inColor;

	gl_Position = ubo.projection * ubo.view * vec4(inPosition, 1.0);
}