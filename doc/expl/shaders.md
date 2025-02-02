Fragment and vertex shader parameters
=====================================

`node.gl` exposes various parameters to the shaders:


 - parameters with the `ngl_` prefix, always available
 - parameters from the `Render` node (`attributes`, `buffers`, `textures`, and
   `uniforms`), with names derived from the arbitrary dict keys specified
   by the user

## Main parameters (always available)

Qualifier | Type   | Name                       | Description
----------|--------|----------------------------|------------
in        | `vec4` | `ngl_position`             | geometry vertex attribute
in        | `vec2` | `ngl_uvcoord`              | geometry uv coordinate attribute
in        | `vec3` | `ngl_normal`               | geometry normal attribute
uniform   | `mat4` | `ngl_modelview_matrix`     | modelview matrix
uniform   | `mat4` | `ngl_projection_matrix`    | projection matrix
uniform   | `mat3` | `ngl_normal_matrix`        | normal matrix

## Texture parameters

`Render.textures` parameters are exposed to the `vertex` and `fragment` shaders
using names derived from their respective dict parameters keys.
For example, the following scene script:

```python
    geometry = Geometry()
    texture0 = Texture2D()
    render = Render(geometry)
    render.update_textures(tex0=texture0)
```

Gives the following shader parameters:

```glsl
    uniform mat4               tex0_coord_matrix;
    uniform vec2               tex0_dimensions;
    uniform int                tex0_sampling_mode;
    uniform sampler2D          tex0_sampler;
    /* Android only */
    uniform samplerExternalOES tex0_external_sampler;
    /* Linux and iOS */
    uniform sampler2D          tex0_y_sampler;
    uniform sampler2D          tex0_uv_sampler;
    /* macOS only */
    uniform sampler2DRect      tex0_y_rect_sampler;
    uniform sampler2DRect      tex0_uv_rect_sampler;
```

The following table describes these parameters:

Qualifier | Type                        | Name                       | Description
----------|-----------------------------|----------------------------|------------
uniform   | `mat4`                      | `%s_coord_matrix`          | uv transformation matrix of the texture associated with the render node using key `%s`, it should be applied to the geometry uv coordinates `ngl_uvcoord` to obtain the final texture coordinates
uniform   | `vec2`                      | `%s_dimensions`            | dimensions in pixels of the texture associated with the render node using key `%s`
uniform   | `sampler2D`, `sampler3D`    | `%s_sampler`               | sampler of the texture associated with the render node using key `%s`
uniform   | `samplerExternalOES`, `__samplerExternal2DY2YEXT` | `%s_external_sampler` | external `OES` (Android only) or `2DY2YEXT` (Android with `GL_EXT_YUV_target` only) sampler of the texture associated with the render node using key `%s`
uniform   | `sampler2D`                 | `%s_y_sampler`             | luminance sampler (Linux and iOS) of the texture associated with the render node using key `%s`
uniform   | `sampler2D`                 | `%s_uv_sampler`            | chrominance sampler (Linux and iOS) of the texture associated with the render node using key `%s`
uniform   | `sampler2DRect`             | `%s_y_rect_sampler`        | luminance rectangle sampler (macOS only) of the texture associated with the render node using key `%s`
uniform   | `sampler2DRect`             | `%s_uv_rect_sampler`       | chrominance rectangle sampler (macOS only) of the texture associated with the render node using key `%s`
uniform   | `int`                       | `%s_sampling_mode`         | sampling mode used by the texture nodes associated with the render node using key `%s`, it indicates from which sampler the color should be picked from: `1` for standard 2D/3D sampling, `2` for external OES sampling on Android, `3` for NV12 sampling on Linux and iOS, `4` for NV12 rectangle sampling on macOS
uniform   | `float`                     | `%s_ts`                    | timestamp generated by the texture data source, 0.0f for images and buffers, frame timestamp for audios and videos

## Attribute parameters

`Render.attributes` parameters are exposed to the `vertex` shaders using names
derived from their respective dict parameters keys.
For example, the following scene script:

```python
    center_buffer = BufferVec3()
    color_buffer = BufferVec4()
    geometry = Geometry()
    render = Render(geometry)
    render.update_attributes(center=center_buffer, color=color_buffer)
```

Gives the following shader parameter:

```glsl
    in vec3 center;
    in vec4 color;
```

## Uniform parameters

`Render.uniforms` parameters are exposed to the `vertex` and `fragment` shaders
using names derived from their respective dict parameters keys.
For example, the following scene script:

```python
    ucolor1 = UniformVec4()
    ucolor2 = UniformVec4()
    umatrix = UniformMat4()
    render = Render(geometry)
    render.update_uniforms(color1=ucolor1, color2=ucolor2, matrix=umatrix)
```

Gives the following shader parameters:

```glsl
    uniform vec4 color1;
    uniform vec4 color2;
    uniform mat4 matrix;
```

## Block parameters

`Render.blocks` parameters are exposed to the `vertex` and `fragment` shaders
using names derived from their respective dict parameters keys.

For example, the following scene script:

```python
    histogram_block = Block(fields=[UniformFloat(), BufferVec4(256)])
    render = Render(geometry)
    render.update_buffers(histogram=histogram_block)
```

Gives the following shader parameters as SSBO with
`histogram_block.set_layout('std430')`:

```glsl
    layout (std430, binding=0) buffer histogram {
        float maximum;
        vec4 data[];
    };
```

Or gives the following shader parameters as UBO with
`histogram_block.set_layout('std140')`:

```glsl
    layout (std140, binding=0) uniform histogram {
        float maximum;
        vec4 data[256];
    };
```

## Default vertex and fragment shaders

The `Program` node provides default `vertex` and `fragment` shaders if none are
provided by the user.

### Vertex shader

```glsl
#version 100

precision highp float;
attribute vec4 ngl_position;
attribute vec2 ngl_uvcoord;
attribute vec3 ngl_normal;
uniform mat4 ngl_modelview_matrix;
uniform mat4 ngl_projection_matrix;
uniform mat3 ngl_normal_matrix;

uniform mat4 tex0_coord_matrix;

varying vec2 var_uvcoord;
varying vec3 var_normal;
varying vec2 var_tex0_coord;

void main()
{
    /* Compute current vertex position */
    gl_Position = ngl_projection_matrix * ngl_modelview_matrix * ngl_position;

    /* Forward geometry uv coordinates to the next stage */
    var_uvcoord = ngl_uvcoord;

    /* Compute current normal coordinates */
    var_normal = ngl_normal_matrix * ngl_normal;

    /* Compute tex0 texture coordinates by applying the corresponding uv
        transformation matrix */
    var_tex0_coord = (tex0_coord_matrix * vec4(ngl_uvcoord, 0, 1)).xy;
}
```

### Fragment shader

```glsl
#version 100

precision highp float;
uniform sampler2D tex0_sampler;
varying vec2 var_uvcoord;
varying vec2 var_tex0_coord;

void main(void)
{
    /* Return corresponding color from the tex0 texture */
    gl_FragColor = texture2D(tex0_sampler, var_tex0_coord);
}
```
