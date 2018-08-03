#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec4 out_color;

layout(binding = 0) uniform Parameters {
    vec4 color;
} parameters;

void main() {
    out_color = parameters.color;
}
