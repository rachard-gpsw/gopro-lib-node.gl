#version 450
#extension GL_ARB_separate_shader_objects : enable

//precision highp float;

out gl_PerVertex {
    vec4 gl_Position;
};

/* node.gl */
layout(location = 0) in vec3 ngl_position;
layout(location = 1) in vec2 ngl_uvcoord;

layout(push_constant) uniform ngl_block {
    mat4 modelview_matrix;
    mat4 projection_matrix;
} ngl;

layout(binding = 1) uniform ngl_uniforms_block {
    mat4 tex0_coord_matrix;
    vec2 tex0_dimensions;
    float tex0_ts;
} ngl_uniforms;

/* custom */
layout(location = 1) out vec2 fragTexCoord;

void main()
{
    gl_Position = ngl.projection_matrix * ngl.modelview_matrix * vec4(ngl_position, 1.0);
    //fragTexCoord = ngl_uvcoord * vec2(1.0, -1.0);
    fragTexCoord = (ngl_uniforms.tex0_coord_matrix * vec4(ngl_uvcoord, 0.0, 1.0)).xy;
    //var_uvcoord = ngl_uvcoord;
    //var_normal = ngl.normal_matrix * ngl_normal;
    //var_tex0_coord = (tex0_coord_matrix * vec4(ngl_uvcoord, 0, 1)).xy;
}
