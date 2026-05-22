#version 450

layout (location = 0) in vec3 inNormal;

layout (binding = 1) uniform UBO
{
    mat4 projection;
    mat4 model;
    mat4 view;
    vec3 camPos;
};

layout (binding = 2) uniform SHCoeffs
{
    vec3 shCoeffs[9];
};

layout (location = 0) out vec4 outColor;


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

vec3 getIrradiance(vec3 normal)
{
    vec3 irradiance = vec3(0.0);
    for (int i = 0; i < 9; i++)
    {
        irradiance += shCoeffs[i] * SHbasisFlat(i, normal);
    }
    return irradiance;
}

void main()
{
    vec3 normal = normalize(inNormal);

    vec3 irradiance = getIrradiance(normal);
    vec3 diffuse = (1.0 - metallic) * albedo / PI * irradiance;

    vec3 color = diffuse; // + specular (not implemented in this snippet)
    // Tone Mapping
    color = Uncharted2Tonemap(color * exposure);
    // gamma
    color = pow(color, vec3(1.0 / 2.2));
    outColor = vec4(color, 1.0);
}
