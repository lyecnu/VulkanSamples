#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec3 inColor;
layout (location = 3) in vec4 inLightPos;
layout (location = 4) in vec4 inLightSpaceCoord;

layout (binding = 1) uniform sampler2D shadowMap;

layout (constant_id = 0) const int filterSize = 1;

layout (location = 0) out vec4 outFragColor;

#define AMBIENT 0.1

float textureShadowMap(vec4 shadowCoord, vec2 offset)
{
	float shadow = 1.0;
	
	shadowCoord.xy = shadowCoord.xy * 0.5 + 0.5;
	shadowCoord.xy += offset;

	if ( shadowCoord.z > 0.0 && shadowCoord.z < 1.0 ) 
	{
		float closeDepth = texture(shadowMap, shadowCoord.xy).r;
		shadow = closeDepth < shadowCoord.z ? AMBIENT : 1.0;
	}

	return shadow;
}

float filterPCF(vec4 shadowCoord, int size)
{
	float scale = 1;
	
	ivec2 texDim = textureSize(shadowMap, 0);
	float dx = scale * 1.0 / float(texDim.x);
	float dy = scale * 1.0 / float(texDim.y);

	float shadowFactor = 0.0;
	int count = 0;

	for (int x = -size; x <= size; x++)
	{
		for (int y = -size; y <= size; y++)
		{
			shadowFactor += textureShadowMap(shadowCoord, vec2(dx*x, dy*y));
			count++;
		}
	}

	return shadowFactor / count;
}

void main() 
{
	vec4 shadowCoord = inLightSpaceCoord / inLightSpaceCoord.w;
	float shadow = filterPCF(shadowCoord, filterSize);

	vec3 N = normalize(inNormal);
	vec3 L = normalize(inLightPos.xyz - inPos);

	vec3 diffuse = max(dot(N, L), AMBIENT) * inColor;

	outFragColor = vec4(diffuse * shadow, 1.0);
}
