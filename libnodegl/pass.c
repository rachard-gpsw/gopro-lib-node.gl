/*
 * Copyright 2016-2018 GoPro Inc.
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#include "buffer.h"
#include "default_shaders.h"
#include "glincludes.h"
#include "hmap.h"
#include "image.h"
#include "log.h"
#include "math_utils.h"
#include "memory.h"
#include "nodegl.h"
#include "nodes.h"
#include "pass.h"
#include "pipeline.h"
#include "program.h"
#include "texture.h"
#include "topology.h"
#include "type.h"
#include "utils.h"

static int register_uniform(struct pass *s, const char *name, struct ngl_node *uniform)
{
    if (!uniform)
        return 0;

    struct hmap *infos = s->pipeline_program->uniforms;
    if (!ngli_hmap_get(infos, name)) {
        struct pass_params *params = &s->params;
        LOG(WARNING, "uniform %s attached to pipeline %s not found in shader", name, params->label);
        return 0;
    }

    struct pipeline_uniform pipeline_uniform = {{0}};
    snprintf(pipeline_uniform.name, sizeof(pipeline_uniform.name), "%s", name);

    if (uniform->class->category == NGLI_NODE_CATEGORY_BUFFER) {
        struct buffer_priv *buffer_priv = uniform->priv_data;
        pipeline_uniform.type  = buffer_priv->data_type;
        pipeline_uniform.count = buffer_priv->count;
        pipeline_uniform.data  = buffer_priv->data;
    } else if (uniform->class->category == NGLI_NODE_CATEGORY_UNIFORM) {
        struct variable_priv *variable_priv = uniform->priv_data;
        pipeline_uniform.type  = variable_priv->data_type;
        pipeline_uniform.count = 1;
        pipeline_uniform.data  = variable_priv->data;
    } else {
        ngli_assert(0);
    }

    if (!ngli_darray_push(&s->pipeline_uniforms, &pipeline_uniform))
        return NGL_ERROR_MEMORY;

    if (!ngli_darray_push(&s->uniforms, &uniform))
        return NGL_ERROR_MEMORY;

    return 0;
}

static int register_uniforms(struct pass *s)
{
    struct pass_params *params = &s->params;

    struct pipeline_uniform pipeline_uniforms[] = {
        {.name = "ngl_modelview_matrix",  .type = NGLI_TYPE_MAT4, .count = 1, .data = NULL},
        {.name = "ngl_projection_matrix", .type = NGLI_TYPE_MAT4, .count = 1, .data = NULL},
        {.name = "ngl_normal_matrix",     .type = NGLI_TYPE_MAT3, .count = 1, .data = NULL},
    };

    for (int i = 0; i < NGLI_ARRAY_NB(pipeline_uniforms); i++) {
        struct pipeline_uniform *pipeline_uniform = &pipeline_uniforms[i];
        struct hmap *infos = s->pipeline_program->uniforms;
        if (!ngli_hmap_get(infos, pipeline_uniform->name))
            continue;
        if (!ngli_darray_push(&s->pipeline_uniforms, pipeline_uniform))
            return NGL_ERROR_MEMORY;
    }

    if (!params->uniforms)
        return 0;

    const struct hmap_entry *entry = NULL;
    while ((entry = ngli_hmap_next(params->uniforms, entry))) {
        struct ngl_node *uniform = entry->data;
        int ret = register_uniform(s, entry->key, uniform);
        if (ret < 0)
            return ret;
    }

    return 0;
}

struct texture_info_field {
    int active;
    int index;
    int type;
    int is_sampler_or_image;
};

struct texture_info {
    const char *name;
    struct image *image;
    struct texture_info_field sampling_mode;
    struct texture_info_field default_sampler;
    struct texture_info_field coordinate_matrix;
    struct texture_info_field dimensions;
    struct texture_info_field timestamp;
    struct texture_info_field oes_sampler;
    struct texture_info_field y_sampler;
    struct texture_info_field uv_sampler;
    struct texture_info_field y_rect_sampler;
    struct texture_info_field uv_rect_sampler;
};

#define OFFSET(x) offsetof(struct texture_info, x)
static const struct texture_info_map {
    const char *suffix;
    const int *allowed_types;
    size_t field_offset;
} texture_info_maps[] = {
    {"_sampling_mode",    (const int[]){NGLI_TYPE_INT, 0},                         OFFSET(sampling_mode)},
    {"_sampler",          (const int[]){NGLI_TYPE_SAMPLER_2D,
                                        NGLI_TYPE_SAMPLER_3D,
                                        NGLI_TYPE_SAMPLER_CUBE,
                                        NGLI_TYPE_IMAGE_2D, 0},                    OFFSET(default_sampler)},
    {"_coord_matrix",     (const int[]){NGLI_TYPE_MAT4, 0},                        OFFSET(coordinate_matrix)},
    {"_dimensions",       (const int[]){NGLI_TYPE_VEC2,
                                        NGLI_TYPE_VEC3, 0},                        OFFSET(dimensions)},
    {"_ts",               (const int[]){NGLI_TYPE_FLOAT, 0},                       OFFSET(timestamp)},
    {"_external_sampler", (const int[]){NGLI_TYPE_SAMPLER_EXTERNAL_OES,
                                        NGLI_TYPE_SAMPLER_EXTERNAL_2D_Y2Y_EXT, 0}, OFFSET(oes_sampler)},
    {"_y_sampler",        (const int[]){NGLI_TYPE_SAMPLER_2D, 0},                  OFFSET(y_sampler)},
    {"_uv_sampler",       (const int[]){NGLI_TYPE_SAMPLER_2D, 0},                  OFFSET(uv_sampler)},
    {"_y_rect_sampler",   (const int[]){NGLI_TYPE_SAMPLER_2D_RECT, 0},             OFFSET(y_rect_sampler)},
    {"_uv_rect_sampler",  (const int[]){NGLI_TYPE_SAMPLER_2D_RECT, 0},             OFFSET(uv_rect_sampler)},
};

static int is_allowed_type(const int *allowed_types, int type)
{
    for (int i = 0; allowed_types[i]; i++)
        if (allowed_types[i] == type)
            return 1;
    return 0;
}

static int is_sampler_or_image(int type)
{
    switch (type) {
    case NGLI_TYPE_SAMPLER_2D:
    case NGLI_TYPE_SAMPLER_2D_RECT:
    case NGLI_TYPE_SAMPLER_3D:
    case NGLI_TYPE_SAMPLER_CUBE:
    case NGLI_TYPE_SAMPLER_EXTERNAL_OES:
    case NGLI_TYPE_SAMPLER_EXTERNAL_2D_Y2Y_EXT:
    case NGLI_TYPE_IMAGE_2D:
        return 1;
    default:
        return 0;
    }
}

static int register_texture(struct pass *s, const char *name, struct ngl_node *texture)
{
    if (!texture)
        return 0;

    struct texture_priv *texture_priv = texture->priv_data;
    struct image *image = &texture_priv->image;

    struct texture_info info = {
        .name  = name,
        .image = image,
    };

    struct hmap *infos = s->pipeline_program->uniforms;
    for (int i = 0; i < NGLI_ARRAY_NB(texture_info_maps); i++) {
        const struct texture_info_map *map = &texture_info_maps[i];

        uint8_t *info_p = (uint8_t *)&info + map->field_offset;
        struct texture_info_field *field = (struct texture_info_field *)info_p;

        char uniform_name[MAX_ID_LEN];
        snprintf(uniform_name, sizeof(uniform_name), "%s%s", name, map->suffix);

        const struct uniformprograminfo *uniform = ngli_hmap_get(infos, uniform_name);
        if (!uniform)
            continue;

        if (!is_allowed_type(map->allowed_types, uniform->type)) {
            LOG(ERROR, "invalid type 0x%x found for texture uniform %s",
                uniform->type, uniform_name);
            return NGL_ERROR_INVALID_ARG;
        }
        field->active = 1;
        field->type = uniform->type;
        field->is_sampler_or_image = is_sampler_or_image(uniform->type);

        if (field->is_sampler_or_image) {
            struct pipeline_texture pipeline_texture = {
                .name = {0}
            };
            snprintf(pipeline_texture.name, sizeof(pipeline_texture.name), "%s", uniform_name);
            if (!ngli_darray_push(&s->pipeline_textures, &pipeline_texture))
                return NGL_ERROR_MEMORY;
        } else {
            struct pipeline_uniform pipeline_uniform = {
                .type  = uniform->type,
                .count = 1
            };
            snprintf(pipeline_uniform.name, sizeof(pipeline_uniform.name), "%s", uniform_name);
            if (!ngli_darray_push(&s->pipeline_uniforms, &pipeline_uniform))
                return NGL_ERROR_MEMORY;
        }
    }

    uint32_t supported_image_layouts = 0;

    if (info.default_sampler.active)
        supported_image_layouts |= 1 << NGLI_IMAGE_LAYOUT_DEFAULT;

    if (info.oes_sampler.active)
        supported_image_layouts |= 1 << NGLI_IMAGE_LAYOUT_MEDIACODEC;

    if (info.y_sampler.active || info.uv_sampler.active)
        supported_image_layouts |= 1 << NGLI_IMAGE_LAYOUT_NV12;

    if (info.y_rect_sampler.active || info.uv_rect_sampler.active)
        supported_image_layouts |= 1 << NGLI_IMAGE_LAYOUT_NV12_RECTANGLE;

    if (!supported_image_layouts)
        LOG(WARNING, "no sampler found for texture %s", name);

    texture_priv->supported_image_layouts &= supported_image_layouts;

    if (!ngli_darray_push(&s->texture_infos, &info))
        return NGL_ERROR_MEMORY;

    if (!ngli_darray_push(&s->textures, &texture))
        return NGL_ERROR_MEMORY;

    return 0;
}

static int register_textures(struct pass *s)
{
    struct pass_params *params = &s->params;

    if (!params->textures)
        return 0;

    const struct hmap_entry *entry = NULL;
    while ((entry = ngli_hmap_next(params->textures, entry))) {
        struct ngl_node *texture = entry->data;
        int ret = register_texture(s, entry->key, texture);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static int register_block(struct pass *s, const char *name, struct ngl_node *block)
{
    if (!block)
        return 0;

    struct hmap *infos = s->pipeline_program->buffer_blocks;
    if (!ngli_hmap_get(infos, name)) {
        struct pass_params *params = &s->params;
        LOG(WARNING, "block %s attached to pipeline %s not found in shader", name, params->label);
        return 0;
    }

    struct block_priv *block_priv = block->priv_data;
    struct buffer *buffer = &block_priv->buffer;
    struct pipeline_buffer pipeline_buffer = {
        .buffer = buffer,
    };
    snprintf(pipeline_buffer.name, sizeof(pipeline_buffer.name), "%s", name);

    if (!ngli_darray_push(&s->pipeline_buffers, &pipeline_buffer))
        return NGL_ERROR_MEMORY;

    if (!ngli_darray_push(&s->blocks, &block))
        return NGL_ERROR_MEMORY;

    int ret = ngli_node_block_ref(block);
    if (ret < 0)
        return ret;

    return 0;
}

static int register_blocks(struct pass *s)
{
    struct pass_params *params = &s->params;

    if (!params->blocks)
        return 0;

    const struct hmap_entry *entry = NULL;
    while ((entry = ngli_hmap_next(params->blocks, entry))) {
        struct ngl_node *block = entry->data;
        int ret = register_block(s, entry->key, block);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static int check_attributes(struct pass *s, struct hmap *attributes, int per_instance)
{
    if (!attributes)
        return 0;

    const struct hmap_entry *entry = NULL;
    while ((entry = ngli_hmap_next(attributes, entry))) {
        struct ngl_node *anode = entry->data;
        struct buffer_priv *buffer = anode->priv_data;

        if (per_instance) {
            if (buffer->count != s->params.nb_instances) {
                LOG(ERROR, "attribute buffer %s count (%d) does not match instance count (%d)",
                    entry->key, buffer->count, s->params.nb_instances);
                return NGL_ERROR_INVALID_ARG;
            }
        } else {
            struct geometry_priv *geometry = s->params.geometry->priv_data;
            struct buffer_priv *vertices = geometry->vertices_buffer->priv_data;
            if (buffer->count != vertices->count) {
                LOG(ERROR, "attribute buffer %s count (%d) does not match vertices count (%d)",
                    entry->key, buffer->count, vertices->count);
                return NGL_ERROR_INVALID_ARG;
            }
        }
    }
    return 0;
}

static int register_attribute(struct pass *s, const char *name, struct ngl_node *attribute, int rate, int warn)
{
    if (!attribute)
        return 0;

    struct hmap *infos = s->pipeline_program->attributes;
    if (!ngli_hmap_get(infos, name)) {
        if (warn) {
            const struct pass_params *params = &s->params;
            LOG(WARNING, "attribute %s attached to pipeline %s not found in shader", name, params->label);
        }
        return 0;
    }

    int ret = ngli_node_buffer_ref(attribute);
    if (ret < 0)
        return ret;

    struct buffer_priv *attribute_priv = attribute->priv_data;
    int stride = attribute_priv->data_stride;
    int offset = 0;
    struct buffer *buffer = &attribute_priv->buffer;

    if (attribute_priv->block) {
        struct block_priv *block = attribute_priv->block->priv_data;
        const struct block_field_info *fi = &block->field_info[attribute_priv->block_field];
        stride = fi->stride;
        offset = fi->offset;
        buffer = &block->buffer;
    }

    struct pipeline_attribute pipeline_attribute = {
        .format = attribute_priv->data_format,
        .stride = stride,
        .offset = offset,
        .count  = 1,
        .rate   = rate,
        .buffer = buffer,
    };
    snprintf(pipeline_attribute.name, sizeof(pipeline_attribute.name), "%s", name);

    if (!ngli_darray_push(&s->pipeline_attributes, &pipeline_attribute))
        return NGL_ERROR_MEMORY;

    if (!ngli_darray_push(&s->attributes, &attribute))
        return NGL_ERROR_MEMORY;

    return 0;
}

static int pass_graphics_init(struct pass *s)
{
    struct ngl_ctx *ctx = s->ctx;
    const struct pass_params *params = &s->params;

    s->pipeline_type = NGLI_PIPELINE_TYPE_GRAPHICS;

    if (!s->pipeline_program) {
        const char *vertex = ngli_get_default_shader(NGLI_PROGRAM_SHADER_VERT);
        const char *fragment = ngli_get_default_shader(NGLI_PROGRAM_SHADER_FRAG);
        int ret = ngli_program_init(&s->default_program, ctx, vertex, fragment, NULL);
        if (ret < 0)
            return ret;
        s->pipeline_program = &s->default_program;
    }

    struct geometry_priv *geometry_priv = params->geometry->priv_data;
    struct pipeline_graphics *graphics = &s->pipeline_graphics;

    graphics->topology = geometry_priv->topology;
    graphics->nb_instances = s->params.nb_instances;

    if (geometry_priv->indices_buffer) {
        struct ngl_node *indices = geometry_priv->indices_buffer;
        int ret = ngli_node_buffer_ref(indices);
        if (ret < 0)
            return ret;

        struct buffer_priv *indices_priv = indices->priv_data;
        if (indices_priv->block) {
            LOG(ERROR, "geometry indices buffers referencing a block are not supported");
            return NGL_ERROR_UNSUPPORTED;
        }

        s->indices = indices;
        s->indices_buffer = &indices_priv->buffer;

        graphics->nb_indices = indices_priv->count;
        graphics->indices_format = indices_priv->data_format;
        graphics->indices = &indices_priv->buffer;
    } else {
        struct ngl_node *vertices = geometry_priv->vertices_buffer;
        struct buffer_priv *buffer_priv = vertices->priv_data;
        graphics->nb_vertices = buffer_priv->count;
    }

    int ret;
    if ((ret = check_attributes(s, params->attributes, 0)) < 0 ||
        (ret = check_attributes(s, params->instance_attributes, 1)) < 0)
        return ret;

    if ((ret = register_attribute(s, "ngl_position", geometry_priv->vertices_buffer, 0, 0)) < 0 ||
        (ret = register_attribute(s, "ngl_uvcoord",  geometry_priv->uvcoords_buffer, 0, 0)) < 0 ||
        (ret = register_attribute(s, "ngl_normal",   geometry_priv->normals_buffer,  0, 0)) < 0)
        return ret;

    if (params->attributes) {
        const struct hmap_entry *entry = NULL;
        while ((entry = ngli_hmap_next(params->attributes, entry))) {
            int ret = register_attribute(s, entry->key, entry->data, 0, 1);
            if (ret < 0)
                return ret;
        }
    }

    if (params->instance_attributes) {
        const struct hmap_entry *entry = NULL;
        while ((entry = ngli_hmap_next(params->instance_attributes, entry))) {
            int ret = register_attribute(s, entry->key, entry->data, 1, 1);
            if (ret < 0)
                return ret;
        }
    }

    return 0;
}

static int pass_compute_init(struct pass *s)
{
    const struct pass_params *params = &s->params;

    s->pipeline_type = NGLI_PIPELINE_TYPE_COMPUTE;
    s->pipeline_compute.nb_group_x = params->nb_group_x;
    s->pipeline_compute.nb_group_y = params->nb_group_y;
    s->pipeline_compute.nb_group_z = params->nb_group_z;

    return 0;
}

int ngli_pass_init(struct pass *s, struct ngl_ctx *ctx, const struct pass_params *params)
{
    s->ctx = ctx;
    s->params = *params;

    ngli_darray_init(&s->attributes, sizeof(struct ngl_node *), 0);
    ngli_darray_init(&s->textures, sizeof(struct ngl_node *), 0);
    ngli_darray_init(&s->uniforms, sizeof(struct ngl_node *), 0);
    ngli_darray_init(&s->blocks, sizeof(struct ngl_node *), 0);

    ngli_darray_init(&s->texture_infos, sizeof(struct texture_info), 0);

    ngli_darray_init(&s->pipeline_attributes, sizeof(struct pipeline_attribute), 0);
    ngli_darray_init(&s->pipeline_textures, sizeof(struct pipeline_texture), 0);
    ngli_darray_init(&s->pipeline_uniforms, sizeof(struct pipeline_uniform), 0);
    ngli_darray_init(&s->pipeline_buffers, sizeof(struct pipeline_buffer), 0);

    if (params->program) {
        struct program_priv *program_priv = params->program->priv_data;
        s->pipeline_program = &program_priv->program;
    }

    int ret = params->geometry ? pass_graphics_init(s)
                               : pass_compute_init(s);
    if (ret < 0)
        return ret;

    if ((ret = register_uniforms(s)) < 0 ||
        (ret = register_textures(s)) < 0 ||
        (ret = register_blocks(s)) < 0)
        return ret;

    struct pipeline_params pipeline_params = {
        .type          = s->pipeline_type,
        .graphics      = s->pipeline_graphics,
        .compute       = s->pipeline_compute,
        .program       = s->pipeline_program,
        .attributes    = ngli_darray_data(&s->pipeline_attributes),
        .nb_attributes = ngli_darray_count(&s->pipeline_attributes),
        .uniforms      = ngli_darray_data(&s->pipeline_uniforms),
        .nb_uniforms   = ngli_darray_count(&s->pipeline_uniforms),
        .textures      = ngli_darray_data(&s->pipeline_textures),
        .nb_textures   = ngli_darray_count(&s->pipeline_textures),
        .buffers       = ngli_darray_data(&s->pipeline_buffers),
        .nb_buffers    = ngli_darray_count(&s->pipeline_buffers),
    };

    ret = ngli_pipeline_init(&s->pipeline, ctx, &pipeline_params);
    if (ret < 0)
        return ret;

    s->modelview_matrix_index = ngli_pipeline_get_uniform_index(&s->pipeline, "ngl_modelview_matrix");
    s->projection_matrix_index = ngli_pipeline_get_uniform_index(&s->pipeline, "ngl_projection_matrix");
    s->normal_matrix_index = ngli_pipeline_get_uniform_index(&s->pipeline, "ngl_normal_matrix");

    struct texture_info *texture_infos = ngli_darray_data(&s->texture_infos);
    for (int i = 0; i < ngli_darray_count(&s->texture_infos); i++) {
        struct texture_info *info = &texture_infos[i];
        for (int i = 0; i < NGLI_ARRAY_NB(texture_info_maps); i++) {
            const struct texture_info_map *map = &texture_info_maps[i];

            uint8_t *info_p = (uint8_t *)info + map->field_offset;
            struct texture_info_field *field = (struct texture_info_field *)info_p;
            if (!field->active) {
                field->index = -1;
                continue;
            }

            char name[MAX_ID_LEN];
            snprintf(name, sizeof(name), "%s%s", info->name, map->suffix);

            if (field->is_sampler_or_image)
                field->index = ngli_pipeline_get_texture_index(&s->pipeline, name);
            else
                field->index = ngli_pipeline_get_uniform_index(&s->pipeline, name);
        }
    }

    return 0;
}

#define NODE_TYPE_DEFAULT 0
#define NODE_TYPE_BLOCK   1
#define NODE_TYPE_BUFFER  2

static void reset_nodes(struct darray *p, int type)
{
    struct ngl_node **nodes = ngli_darray_data(p);
    for (int i = 0; i < ngli_darray_count(p); i++) {
        if (type == NODE_TYPE_BLOCK)
            ngli_node_block_unref(nodes[i]);
        else if (type == NODE_TYPE_BUFFER)
            ngli_node_buffer_unref(nodes[i]);
    }
    ngli_darray_reset(p);
}

#define DECLARE_RESET_NODES_FUNC(name, type)       \
static void reset_##name##_nodes(struct darray *p) \
{                                                  \
    reset_nodes(p, type);                          \
}                                                  \

DECLARE_RESET_NODES_FUNC(block, NODE_TYPE_BLOCK)
DECLARE_RESET_NODES_FUNC(buffer, NODE_TYPE_BUFFER)

void ngli_pass_uninit(struct pass *s)
{
    if (!s->ctx)
        return;

    ngli_pipeline_reset(&s->pipeline);

    if (s->indices)
        ngli_node_buffer_unref(s->indices);

    ngli_darray_reset(&s->uniforms);
    ngli_darray_reset(&s->textures);
    reset_block_nodes(&s->blocks);
    reset_buffer_nodes(&s->attributes);

    ngli_darray_reset(&s->texture_infos);

    ngli_darray_reset(&s->pipeline_attributes);
    ngli_darray_reset(&s->pipeline_textures);
    ngli_darray_reset(&s->pipeline_uniforms);
    ngli_darray_reset(&s->pipeline_buffers);

    ngli_program_reset(&s->default_program);

    memset(s, 0, sizeof(*s));
}

static int update_nodes(struct darray *p, double t, int type)
{
    struct ngl_node **nodes = ngli_darray_data(p);
    for (int i = 0; i < ngli_darray_count(p); i++) {
        struct ngl_node *node = nodes[i];
        int ret = ngli_node_update(node, t);
        if (ret < 0)
            return ret;
        if (type == NODE_TYPE_BLOCK) {
            ret = ngli_node_block_upload(node);
            if (ret < 0)
                return ret;
        } else if (type == NODE_TYPE_BUFFER) {
            ret = ngli_node_buffer_upload(node);
            if (ret < 0)
                return ret;
        }
    }
    return 0;
}

#define DECLARE_UPDATE_NODES_FUNC(name, type)                \
static int update_##name##_nodes(struct darray *p, double t) \
{                                                            \
    return update_nodes(p, t, type);                         \
}

DECLARE_UPDATE_NODES_FUNC(common, NODE_TYPE_DEFAULT)
DECLARE_UPDATE_NODES_FUNC(block, NODE_TYPE_BLOCK)
DECLARE_UPDATE_NODES_FUNC(buffer, NODE_TYPE_BUFFER)

int ngli_pass_update(struct pass *s, double t)
{
    int ret;
    if ((ret = update_common_nodes(&s->uniforms, t)) < 0 ||
        (ret = update_common_nodes(&s->textures, t)) < 0 ||
        (ret = update_block_nodes(&s->blocks, t)) < 0 ||
        (ret = update_buffer_nodes(&s->attributes, t)))
        return ret;

    return 0;
}

int ngli_pass_exec(struct pass *s)
{
    struct ngl_ctx *ctx = s->ctx;

    const float *modelview_matrix = ngli_darray_tail(&ctx->modelview_matrix_stack);
    const float *projection_matrix = ngli_darray_tail(&ctx->projection_matrix_stack);

    ngli_pipeline_update_uniform(&s->pipeline, s->modelview_matrix_index, modelview_matrix);
    ngli_pipeline_update_uniform(&s->pipeline, s->projection_matrix_index, projection_matrix);

    if (s->normal_matrix_index >= 0) {
        float normal_matrix[3*3];
        ngli_mat3_from_mat4(normal_matrix, modelview_matrix);
        ngli_mat3_inverse(normal_matrix, normal_matrix);
        ngli_mat3_transpose(normal_matrix, normal_matrix);
        ngli_pipeline_update_uniform(&s->pipeline, s->normal_matrix_index, normal_matrix);
    }

    struct texture_info *texture_infos = ngli_darray_data(&s->texture_infos);
    for (int i = 0; i < ngli_darray_count(&s->texture_infos); i++) {
        struct texture_info *info = &texture_infos[i];
        struct image *image = info->image;
        const float ts = image->ts;

        ngli_pipeline_update_uniform(&s->pipeline, info->coordinate_matrix.index, image->coordinates_matrix);
        ngli_pipeline_update_uniform(&s->pipeline, info->timestamp.index, &ts);

        if (image->layout) {
            const struct texture *plane = image->planes[0];
            const struct texture_params *plane_params = &plane->params;
            const float dimensions[] = {plane_params->width, plane_params->height, plane_params->depth};
            ngli_pipeline_update_uniform(&s->pipeline, info->dimensions.index, dimensions);
        }

        int ret = -1;
        switch (image->layout) {
        case NGLI_IMAGE_LAYOUT_DEFAULT:
            ret = ngli_pipeline_update_texture(&s->pipeline, info->default_sampler.index, image->planes[0]);
            break;
        case NGLI_IMAGE_LAYOUT_NV12:
            ret = ngli_pipeline_update_texture(&s->pipeline, info->y_sampler.index, image->planes[0]);
            ret &= ngli_pipeline_update_texture(&s->pipeline, info->uv_sampler.index, image->planes[1]);
            break;
        case NGLI_IMAGE_LAYOUT_NV12_RECTANGLE:
            ret = ngli_pipeline_update_texture(&s->pipeline, info->y_rect_sampler.index, image->planes[0]);
            ret &= ngli_pipeline_update_texture(&s->pipeline, info->uv_rect_sampler.index, image->planes[1]);
            break;
        case NGLI_IMAGE_LAYOUT_MEDIACODEC:
            ret = ngli_pipeline_update_texture(&s->pipeline, info->oes_sampler.index, image->planes[0]);
        default:
            break;
        };
        const int layout = ret < 0 ? NGLI_IMAGE_LAYOUT_NONE : image->layout;
        ngli_pipeline_update_uniform(&s->pipeline, info->sampling_mode.index, &layout);
    }

    ngli_pipeline_exec(&s->pipeline);

    return 0;
}
