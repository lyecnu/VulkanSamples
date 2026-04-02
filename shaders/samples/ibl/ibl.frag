#version 450

const float PI = 3.14159265359;

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inNormal;

layout (binding = 0) uniform UBO {
	mat4 projection;
	mat4 model;
	mat4 view;
	vec3 camPos;
} ubo;

layout (binding = 1) uniform UBOShared {
	vec4 lights[4];
} uboParams;

layout(push_constant) uniform PushConstants {
    layout(offset = 12) float roughness;
    float metallic;
    float r;
    float g;
    float b;
} material;

layout(location = 0) out vec4 outColor;

vec3 materialcolor()
{
    return vec3(material.r, material.g, material.b);
}

float D_GGX(float dotNH, float roughness)
{
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float denom = dotNH * dotNH * (alpha2 - 1.0) + 1.0;
    return alpha2 / (PI * denom * denom);
}

float G_SchlicksmithGGX(float dotNL, float dotNV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    float GL = dotNL / (dotNL * (1.0 - k) + k);
    float GV = dotNV / (dotNV * (1.0 - k) + k);
    return GL * GV;
}

vec3 F_Schlick(float cosTheta, float metallic)
{
    vec3 F0 = mix(vec3(0.04), materialcolor(), metallic);
    vec3 F = F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
    return F;
}

vec3 BRDF(vec3 L, vec3 V, vec3 N, float roughness, float metallic)
{
    vec3 H = normalize(L + V);
    float dotNH = max(dot(N, H), 0.0);
    float dotNL = max(dot(N, L), 0.0);
    float dotNV = max(dot(N, V), 0.0);
    float dotVH = max(dot(V, H), 0.0);

    vec3 lightColor = vec3(1.0); // Assuming white light for simplicity

    vec3 color = vec3(0.0);

    if (dotNL > 0.0)
    {
        float D = D_GGX(dotNH, roughness);
        float G = G_SchlicksmithGGX(dotNL, dotNV, roughness);
        vec3 F = F_Schlick(dotVH, metallic);
        
        vec3 specular = D * G * F / (4.0 * dotNL * dotNV + 0.001);

        color += specular * lightColor * dotNL;
    }

    return color;
}

void main()
{
    vec3 V = normalize(ubo.camPos - inWorldPos);
    vec3 N = normalize(inNormal);

    vec3 Lo = vec3(0.0, 0.0, 0.0);
    for (int i = 0; i < uboParams.lights.length(); ++i)
    {
        vec3 L = normalize(uboParams.lights[i].xyz - inWorldPos);
        Lo += BRDF(L, V, N, material.roughness, material.metallic);
    }

    vec3 ambient = vec3(0.02) * materialcolor();
    vec3 color = ambient + Lo;
    color = pow(color, vec3(0.4545));

    outColor = vec4(color, 1.0);
}
