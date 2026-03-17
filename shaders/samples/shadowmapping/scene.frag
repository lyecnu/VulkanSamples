#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec3 inColor;
layout (location = 3) in vec4 inLightSpaceCoord;

layout (binding = 0) uniform UBO 
{
	mat4 projection;
	mat4 view;
	mat4 lightSpaceMVP;
	vec4 lightPos;
	float zNear;
	float zFar;
	float lightSizeUV;
	int filterSize;
} ubo;

layout (binding = 1) uniform sampler2D shadowMap;

layout (constant_id = 0) const int enablePCSS = 0;

layout (location = 0) out vec4 outFragColor;

#define AMBIENT 0.1

float linearDepth(float depth)
{
    return (ubo.zNear * ubo.zFar) / (ubo.zFar + depth * (ubo.zNear - ubo.zFar));	
}

float sampleShadowMapDepth(vec4 shadowCoord, vec2 offset)
{
    vec2 uv = shadowCoord.xy * 0.5 + 0.5 + offset;
    if (uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0 &&
        shadowCoord.z > 0.0 && shadowCoord.z < 1.0)
    {
        return texture(shadowMap, uv).r;
    }
    return 1.0;
}

float filterPCF(vec4 shadowCoord, int size, float dx, float dy)
{
	float shadowFactor = 0.0;
	int count = 0;

	for (int x = -size; x <= size; x++)
	{
		for (int y = -size; y <= size; y++)
		{
			count++;
			float sampleDepth = sampleShadowMapDepth(shadowCoord, vec2(dx*x, dy*y));
			shadowFactor += (shadowCoord.z < sampleDepth) ? 1.0 : 0.0;
		}
	}

	return shadowFactor / count;
}

float calculateBlockerDepth(vec4 shadowCoord, float dx, float dy)
{
	float avgDepth = 0.0;
	int blockerCount = 0;

	float receiverDepth = linearDepth(shadowCoord.z);
	float blockerSize = ubo.lightSizeUV * (1  - ubo.zNear / receiverDepth);

	int searchRange = int(clamp(ceil(blockerSize / (2.0f * max(dx, dy))), 1, 10));

	for (int x = -searchRange; x <= searchRange; x++)
	{
		for (int y = -searchRange; y <= searchRange; y++)
		{
			float sampleDepth = sampleShadowMapDepth(shadowCoord, vec2(dx*x, dy*y));
			if (sampleDepth < shadowCoord.z) 
			{
				avgDepth += linearDepth(sampleDepth);
				blockerCount++;
			}
		}
	}

	return blockerCount > 0 ? avgDepth / blockerCount : -1.0;
}

void main() 
{
	vec4 shadowCoord = inLightSpaceCoord / inLightSpaceCoord.w;
	ivec2 texDim = textureSize(shadowMap, 0);
	float dx = 1.0 / float(texDim.x);
	float dy = 1.0 / float(texDim.y);

	float shadow = 1.0;
	
	if (enablePCSS == 1)
	{
		float blockerAvgDepth = calculateBlockerDepth(shadowCoord, dx, dy);

		if (blockerAvgDepth > 0.0)
		{
			float receiverDepth = linearDepth(shadowCoord.z);
			float blockerDepth = blockerAvgDepth;

			float penumbra = ubo.lightSizeUV * (receiverDepth - blockerDepth) /  blockerDepth;

			int pcssFilterSize = int(clamp(ceil(penumbra / (2.0f * max(dx, dy))), 1, 10));

			shadow = filterPCF(shadowCoord, pcssFilterSize, dx, dy);
		}
	}
	else
	{
		shadow = filterPCF(shadowCoord, ubo.filterSize, dx, dy);
	}

	vec3 N = normalize(inNormal);
	vec3 L = normalize(ubo.lightPos.xyz - inPos);
	vec3 diffuse = max(dot(N, L), AMBIENT) * inColor;

	outFragColor = vec4(diffuse * shadow, 1.0);
}
