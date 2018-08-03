#version 450
#extension GL_ARB_separate_shader_objects : enable

out gl_PerVertex {
    vec4 gl_Position;
};

layout(location = 0) in vec3 ngl_position;

layout(location = 1) in vec4 position;

layout(push_constant) uniform ngl_block {
    mat4 modelview_matrix;
    mat4 projection_matrix;
} ngl;

void main()
{
    gl_Position = ngl.projection_matrix * ngl.modelview_matrix * (vec4(ngl_position, 1.0) + position);
}

