#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 1) uniform FragmentParameters {
    vec2 factor0;
    vec4 factor1;
    vec3 factor2;
} parameters;

layout(location = 0) in vec4 input_color;
layout(location = 0) out vec4 output_color;

void main() {
    output_color = vec4(input_color.rg, input_color.b * parameters.factor1.x, input_color.a);
}
