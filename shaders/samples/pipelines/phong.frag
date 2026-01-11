#version 450

layout (location = 0) in vec3 viewPos;
layout (location = 1) in vec3 viewNormal;
layout (location = 2) in vec3 inColor;

layout (location = 0) out vec4 outFragColor;

void main()
{
    vec3 lightColor = vec3(1.0f, 1.0f, 1.0f);

    // ambient
    vec3 ambientStrength = 0.1;
    vec3 ambient = inColor * vec3(1.0);
    
    // diffuse
    vec3 normal = normalize(inNormal);
    vec3 lightDir = normalize(inLightDir - fra)
    vec3 L = normalize(inLightDir);
    vec3 V = normalize(inViewDir);
    vec3 R = reflect(-L, N);

    vec3 diffuse = max(dot(N, L), 0.0) * inColor;

    vec3 specular = inColor * vec3(1.0);
	outFragColor = vec4(ambient + diffuse + specular, 1.0);
}