#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 1) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;
layout(binding = 10) uniform sampler2D tex0_sampler;

layout(binding = 1) uniform ngl_uniforms_block {
    mat4 tex0_coord_matrix;
    vec2 tex0_dimensions;
    float tex0_ts;
} ngl_uniforms;

void main() {
    outColor = texture(tex0_sampler, fragTexCoord);
    outColor = outColor * (1.0 + (ngl_uniforms.tex0_ts / 5.0));
}
