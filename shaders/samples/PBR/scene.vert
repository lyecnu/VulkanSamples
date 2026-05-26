#version 450

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec2 inUV;
layout (location = 3) in vec4 inTangent;

layout (location = 0) out vec3 outWorldPos;
layout (location = 1) out vec3 outNormal;
layout (location = 2) out vec2 outUV;
layout (location = 3) out vec4 outTangent;

layout(binding = 0) uniform Transforms
{
    mat4 projection;
    mat4 view;
    mat4 model;
    vec3 cameraqPos;
};

void main()
{
    outWorldPos = vec3(model * vec4(inPos, 1.0));
    outNormal = mat3(transpose(inverse(model))) * inNormal;
    outTangent = vec4(mat3(model) * inTangent.xyz, inTangent.w);
    outUV = inUV;
    gl_Position = projection * view * model * vec4(inPos, 1.0);
}
