precision highp float;

in vec4 ngl_position;
uniform mat4 ngl_modelview_matrix;
uniform mat4 ngl_projection_matrix;

void main(void)
{
    vec4 position = ngl_position;
    gl_Position = ngl_projection_matrix * ngl_modelview_matrix * position;
}
