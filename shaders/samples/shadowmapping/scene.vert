#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec3 inColor;

layout (binding = 0) uniform UBO 
{
	mat4 projection;
	mat4 view;
	mat4 lightSpaceMVP;
	vec4 lightPos;
} ubo;

layout (location = 0) out vec3 outPos;
layout (location = 1) out vec3 outNormal;
layout (location = 2) out vec3 outColor;
layout (location = 3) out vec4 outLightPos;
layout (location = 4) out vec4 outLightSpaceCoord;

void main() 
{
	outPos = inPos;
	outNormal = inNormal;
	outColor = inColor;
	outLightPos = ubo.lightPos;
	outLightSpaceCoord = ubo.lightSpaceMVP * vec4(inPos, 1.0);

	gl_Position = ubo.projection * ubo.view * vec4(inPos, 1.0);
}
