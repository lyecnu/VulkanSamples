#version 450

layout (binding = 1) uniform sampler2D inSampler2D;

layout (location = 0) in vec2 inUV;
layout (location = 1) in float inLodBias;

layout (location = 0) out vec4 outFragColor;

void main() 
{
	vec4 sampleColor = texture(inSampler2D, inUV, inLodBias);
	outFragColor = sampleColor;
}