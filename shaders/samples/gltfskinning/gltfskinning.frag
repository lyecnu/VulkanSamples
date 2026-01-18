#version 450

layout (set = 2, binding = 0) uniform sampler2D samplerColorMap;

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec3 inColor;
layout (location = 4) in vec3 inLightPos;

layout (location = 0) out vec4 outFragColor;

void main() 
{
	vec4 color = texture(samplerColorMap, inUV) * vec4(inColor, 1.0);

	vec3 lightDir = normalize(inLightPos - inPos);
	vec3 normal = normalize(inNormal);

	vec3 diffuse = max(dot(normal, lightDir), 0.0) * color.rgb;

	vec3 viewDir = normalize(-inPos);
	vec3 reflectDir = reflect(-lightDir, normal);

	vec3 specular = pow(max(dot(viewDir, reflectDir), 0.0), 16) * vec3(1.0, 1.0, 1.0);

	outFragColor = vec4(diffuse + specular, 1.0);		
}