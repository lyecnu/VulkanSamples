#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec3 inColor;
layout (location = 3) in vec4 inLightPos;
layout (location = 4) in vec4 inLightSpaceCoord;

layout (binding = 1) uniform sampler2D shadowMap;

layout (location = 0) out vec4 outFragColor;

float shadowCalculation(vec4 lightSpaceCoord)
{
	float shadow = 1.0;
	
	vec4 shadowCoord = lightSpaceCoord / lightSpaceCoord.w;
	shadowCoord.xy = shadowCoord.xy * 0.5 + 0.5;

	if ( shadowCoord.z > 0.0 && shadowCoord.z < 1.0 ) 
	{
		float closeDepth = texture(shadowMap, shadowCoord.xy).r;
		shadow = closeDepth < shadowCoord.z ? 0.0 : 1.0;
	}

	return shadow;
}

void main() 
{
	float shadow = shadowCalculation(inLightSpaceCoord);

	vec3 N = normalize(inNormal);
	vec3 L = normalize(inLightPos.xyz - inPos);

	vec3 diffuse = max(dot(N, L), 0.1) * inColor;

	outFragColor = vec4(diffuse * shadow, 1.0);
}
