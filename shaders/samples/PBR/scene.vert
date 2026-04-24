#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;

layout(binding = 0) uniform UBO {
    mat4 projection;
    mat4 view;
    mat4 model;
    vec3 cameraPos;
} ubo;

void main() {
    gl_Position = ubo.projection * ubo.view * vec4(inPos, 1.0);
}
