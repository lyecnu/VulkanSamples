#version 450

layout (location = 0) in vec3 inWorldPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec4 inTangent;

layout (binding = 0) uniform Transforms
{
	mat4 projection;
	mat4 view;
	mat4 model;
	vec3 camPos;
};

layout (binding = 1) uniform Params
{
    vec4 lights[4];
    float exposure;
    float gamma;
};

layout (binding = 2) uniform SHCoeffs
{
    vec4 shCoeffs[9];
};

layout (binding = 5) uniform sampler2D albedoMap;
layout (binding = 6) uniform sampler2D normalMap;
layout (binding = 7) uniform sampler2D aoMap;
layout (binding = 8) uniform sampler2D metallicMap;
layout (binding = 9) uniform sampler2D roughnessMap;
    
layout (location = 0) out vec4 outColor;

#define PI 3.1415926535897932384626433832795

// From http://filmicgames.com/archives/75
vec3 Uncharted2Tonemap(vec3 x)
{
	float A = 0.15;
	float B = 0.50;
	float C = 0.10;
	float D = 0.20;
	float E = 0.02;
	float F = 0.30;
	return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;
}

float D_GGX(float dotNH, float roughness)
{
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float denom = (dotNH * dotNH) * (alpha2 - 1.0) + 1.0;
    return (alpha2) / (PI * denom * denom);
}

float G_SchlicksmithGGX(float dotNL, float dotNV, float roughness)
{
    float k = roughness * roughness / 2.0;
    float GL = dotNL / (dotNL * (1.0 - k) + k);
    float GV = dotNV / (dotNV * (1.0 - k) + k);
    return GL * GV;
}

vec3 F_Schlick(float dotVH, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - dotVH, 5.0);
}

vec3 calculateDirectLight(vec3 L, vec3 V, vec3 N, vec3 albedo, float metallic, float roughness, vec3 F0)
{
    vec3 H = normalize(L + V);
    float dotNH = clamp(dot(N, H), 0.0, 1.0);
    float dotNL = clamp(dot(N, L), 0.0, 1.0);
    float dotNV = clamp(dot(N, V), 0.0, 1.0);
    float dotLH = clamp(dot(L, H), 0.0, 1.0);

    vec3 Li = vec3(1.0);

    vec3 Lo = vec3(0.0);
    if (dotNL > 0.0)
    {
        vec3 diffuse = albedo / PI;

        float D = D_GGX(dotNH, roughness);
        float G = G_SchlicksmithGGX(dotNL, dotNV, roughness);
        vec3 F = F_Schlick(dotLH, F0);
        vec3 spec = D * G * F / (4.0 * dotNL * dotNV + 0.001);

        vec3 kD = (1.0 - F) * (1.0 - metallic);
        Lo += (kD * diffuse + spec) * dotNL * Li;
    }

    return Lo;
}

float SHbasis(int l, int m, vec3 dir) {
    float x = dir.x, y = dir.y, z = dir.z;
    if (l == 0) return 0.282094792;          // Y00
    if (l == 1) {
        if (m == -1) return 0.488602512  * y; // Y1-1
        if (m ==  0) return 0.488602512  * z; // Y10
        if (m ==  1) return 0.488602512  * x; // Y11
    }
    if (l == 2) {
        if (m == -2) return 1.092548431  * x * y;           // Y2-2
        if (m == -1) return 1.092548431  * y * z;           // Y2-1
        if (m ==  0) return 0.315391565 * (3.0*z*z - 1.0);  // Y20
        if (m ==  1) return 1.092548431  * x * z;           // Y21
        if (m ==  2) return 0.546274215  * (x*x - y*y);     // Y22
    }
    return 0.0;
}

float SHbasisFlat(int index, vec3 dir)
{
    if (index == 0) return SHbasis(0, 0, dir);
    if (index <= 3) return SHbasis(1, index - 2, dir);
    return SHbasis(2, index - 6, dir);
}

vec3 calculateIrradiance(vec3 normal)
{
    vec3 irradiance = vec3(0.0);
    for (int i = 0; i < 9; i++)
    {
        irradiance += shCoeffs[i].xyz * SHbasisFlat(i, normal);
    }
    return irradiance;
}

vec3 calculateNormal()
{
    vec3 tangentNormal = texture(normalMap, inUV).xyz * 2.0 - 1.0;

    vec3 N = normalize(inNormal);
    vec3 T = normalize(inTangent.xyz);
    vec3 B = cross(N, T) * inTangent.w;

    mat3 TBN = mat3(T, B, N);
    return normalize(TBN * tangentNormal);
}

void main()
{
    vec3 N = calculateNormal();
    vec3 V = normalize(camPos - inWorldPos);

    vec3 albedo = pow(texture(albedoMap, inUV).rgb, vec3(2.2));
    float metallic = texture(metallicMap, inUV).r;
    float roughness = texture(roughnessMap, inUV).r;
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    
    // Direct lighting
    vec3 Lo_direct = vec3(0.0);

    for (int i = 0; i < lights.length(); i++)
    {
        vec3 L = normalize(lights[i].xyz - inWorldPos);
        Lo_direct += calculateDirectLight(L, V, N, albedo, metallic, roughness, F0);
    }

    // Indirect lighting
    vec3 irradiance = calculateIrradiance(N);
    vec3 diffuse = albedo / PI * irradiance;
    
    // kD for indirect lighting
    vec3 F = F_Schlick(max(dot(N, V), 0.0), F0);
    vec3 kD = (1.0 - F) * (1.0 - metallic);

    vec3 specular = vec3(0.0);

    vec3 Lo_indirect = kD * diffuse + specular;

    vec3 color = Lo_direct + Lo_indirect;
    // Tone Mapping
    color = Uncharted2Tonemap(color * exposure);
    color /= Uncharted2Tonemap(vec3(11.2f));
    // Gamma correction
    color = pow(color, vec3(1.0 / gamma));
    outColor = vec4(color, 1.0);
}
