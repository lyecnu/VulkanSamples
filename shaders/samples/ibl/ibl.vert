#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;

layout(binding = 0) uniform UBO {
    mat4 projection;
    mat4 view;
    mat4 model;
    vec3 cameraPos;
} ubo;

layout(push_constant) uniform PushConstants {
    vec3 objPos;
} push;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outNormal;

void main() {
    vec3 localPos = vec3(ubo.model * vec4(inPos, 1.0));
    outWorldPos = localPos + push.objPos;
    outNormal = mat3(ubo.model) * inNormal;
    gl_Position = ubo.projection * ubo.view * vec4(outWorldPos, 1.0);
}
