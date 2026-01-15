#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec3 inColor;

layout (set = 0, binding = 0) uniform UBOScene
{
	mat4 projection;
	mat4 view;
	vec3 lightPos;
} uboScene;

layout(push_constant) uniform PushConsts {
	mat4 model;
} primitive;

layout (location = 0) out vec3 fragPos;
layout (location = 1) out vec3 fragNormal;
layout (location = 2) out vec2 fragUV;
layout (location = 3) out vec3 fragColor;
layout (location = 4) out vec3 fragLightPos;

void main() 
{
	mat4 modelView = uboScene.view * primitive.model;
	fragPos = vec3(modelView * vec4(inPos, 1.0));
	fragNormal = mat3(transpose(inverse(modelView))) * inNormal;
	fragUV = inUV;
	fragColor = inColor;
	fragLightPos = vec3(uboScene.view * vec4(uboScene.lightPos, 1.0));

	gl_Position = uboScene.projection * modelView * vec4(inPos.xyz, 1.0);
}