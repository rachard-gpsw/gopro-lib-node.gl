/*
 * Copyright 2019 GoPro Inc.
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

#include "block.h"

static int get_buffer_stride(const struct variable *var, int layout)
{
    switch (var->type) {
        case NGLI_TYPE_FLOAT:          return sizeof(GLfloat) * (layout == LAYOUT_STD140 ? 4 : 1);
        case NGLI_TYPE_VEC2:           return sizeof(GLfloat) * (layout == LAYOUT_STD140 ? 4 : 2);
        case NGLI_TYPE_VEC3:
        case NGLI_TYPE_VEC4:           return sizeof(GLfloat) * 4;
        case NGLI_TYPE_INT:
        case NGLI_TYPE_UINT:           return sizeof(GLint) * (layout == LAYOUT_STD140 ? 4 : 1);
        case NGLI_TYPE_IVEC2:
        case NGLI_TYPE_UIVEC2:         return sizeof(GLint) * (layout == LAYOUT_STD140 ? 4 : 2);
        case NGLI_TYPE_IVEC3:
        case NGLI_TYPE_UIVEC3:
        case NGLI_TYPE_IVEC4:
        case NGLI_TYPE_UIVEC4:         return sizeof(GLint) * 4;
        case NGLI_TYPE_MAT4:           return sizeof(GLfloat) * 4 * 4;
    }
    return 0;
}

static int get_buffer_size(const struct ngl_node *bnode, int layout)
{
    const struct buffer_priv *b = bnode->priv_data;
    return b->count * get_buffer_stride(bnode, layout);
}

static int get_quat_size(const struct ngl_node *quat, int layout)
{
    struct uniform_priv *quat_priv = quat->priv_data;
    return sizeof(GLfloat) * 4 * (quat_priv->as_mat4 ? 4 : 1);
}

static int get_node_size(const struct ngl_node *node, int layout)
{
    switch (node->class->id) {
        case NGL_NODE_UNIFORMFLOAT:         return sizeof(GLfloat) * 1;
        case NGL_NODE_UNIFORMVEC2:          return sizeof(GLfloat) * 2;
        case NGL_NODE_UNIFORMVEC3:          return sizeof(GLfloat) * 3;
        case NGL_NODE_UNIFORMVEC4:          return sizeof(GLfloat) * 4;
        case NGL_NODE_UNIFORMMAT4:          return sizeof(GLfloat) * 4 * 4;
        case NGL_NODE_UNIFORMINT:           return sizeof(GLint);
        case NGL_NODE_UNIFORMQUAT:          return get_quat_size(node, layout);
        default:                            return get_buffer_size(node, layout);
    }
}

static int get_node_align(const struct ngl_node *node, int layout)
{
    switch (node->class->id) {
        case NGL_NODE_UNIFORMFLOAT:         return sizeof(GLfloat) * 1;
        case NGL_NODE_UNIFORMVEC2:          return sizeof(GLfloat) * 2;
        case NGL_NODE_UNIFORMVEC3:
        case NGL_NODE_UNIFORMVEC4:
        case NGL_NODE_UNIFORMMAT4:
        case NGL_NODE_UNIFORMQUAT:
        case NGL_NODE_BUFFERMAT4:           return sizeof(GLfloat) * 4;
        case NGL_NODE_UNIFORMINT:           return sizeof(GLint);
        default:                            return get_buffer_stride(node, layout);
    }
    return 0;
}

static int get_spec_id(int class_id)
{
    for (int i = 0; i < NGLI_ARRAY_NB(type_specs); i++)
        if (type_specs[i].class_id == class_id)
            return i;
    return -1;
}

#define FEATURES_STD140 (NGLI_FEATURE_UNIFORM_BUFFER_OBJECT | NGLI_FEATURE_SHADER_STORAGE_BUFFER_OBJECT)
#define FEATURES_STD430 (NGLI_FEATURE_SHADER_STORAGE_BUFFER_OBJECT)

int ngli_block_init(struct block *block, struct glcontext *gl, int nb_fields, const struct variable *fields)
{
    s->gl = gl;
    s->fields = fields;

    if (s->layout == NGLI_BLOCK_LAYOUT_STD140 && !(gl->features & FEATURES_STD140)) {
        LOG(ERROR, "std140 blocks are not supported by this context");
        return -1;
    }

    if (s->layout == NGLI_BLOCK_LAYOUT_STD430 && !(gl->features & FEATURES_STD430)) {
        LOG(ERROR, "std430 blocks are not supported by this context");
        return -1;
    }

    s->field_info = ngli_calloc(s->nb_fields, sizeof(*s->field_info));
    if (!s->field_info)
        return -1;

    s->usage = GL_STATIC_DRAW;

    s->data_size = 0;
    for (int i = 0; i < s->nb_fields; i++) {
        const struct ngl_node *field_node = s->fields[i];
        const int spec_id = get_spec_id(field_node->class->id);
        const int size    = get_node_size(field_node, s->layout);
        const int align   = get_node_align(field_node, s->layout);

        ngli_assert(spec_id >= 0 && spec_id < NGLI_ARRAY_NB(type_specs));
        ngli_assert(align);

        const int remain = s->data_size % align;
        const int offset = s->data_size + (remain ? align - remain : 0);

        const struct type_spec *spec = &type_specs[spec_id];
        if (spec->has_changed(field_node))
            s->usage = GL_DYNAMIC_DRAW;

        struct block_field_info *fi = &s->field_info[i];
        fi->spec_id = spec_id;
        fi->size    = size;
        fi->stride  = get_buffer_stride(field_node, s->layout);
        fi->offset  = offset;

        s->data_size = offset + fi->size;
        LOG(DEBUG, "%s.field[%d]: %s offset=%d size=%d stride=%d",
            node->label, i, field_node->label, fi->offset, fi->size, fi->stride);
    }

    LOG(DEBUG, "total %s size: %d", node->label, s->data_size);
    s->data = ngli_calloc(1, s->data_size);
    if (!s->data)
        return -1;

    return 0;
}

static void update_uniform_field(uint8_t *dst,
                                 const struct ngl_node *node,
                                 const struct block_field_info *fi)
{
    const struct uniform_priv *uniform = node->priv_data;
    memcpy(dst, uniform->data, uniform->data_size);
}

static void update_buffer_field(uint8_t *dst,
                                const struct ngl_node *node,
                                const struct block_field_info *fi)
{
    const struct buffer_priv *buffer = node->priv_data;
    if (buffer->data_stride == fi->stride)
        memcpy(dst, buffer->data, fi->size);
    else
        for (int i = 0; i < buffer->count; i++)
            memcpy(dst + i * fi->stride, buffer->data + i * buffer->data_stride, buffer->data_stride);
}

static const struct type_spec {
    int class_id;
    int (*has_changed)(const struct ngl_node *node);
    void (*update_data)(uint8_t *dst, const struct ngl_node *node, const struct block_field_info *fi);
} type_specs[] = {
    {NGL_NODE_BUFFERFLOAT,         has_changed_buffer},
    {NGL_NODE_BUFFERVEC2,          has_changed_buffer},
    {NGL_NODE_BUFFERVEC3,          has_changed_buffer},
    {NGL_NODE_BUFFERVEC4,          has_changed_buffer},
    {NGL_NODE_BUFFERINT,           has_changed_buffer},
    {NGL_NODE_BUFFERIVEC2,         has_changed_buffer},
    {NGL_NODE_BUFFERIVEC3,         has_changed_buffer},
    {NGL_NODE_BUFFERIVEC4,         has_changed_buffer},
    {NGL_NODE_BUFFERUINT,          has_changed_buffer},
    {NGL_NODE_BUFFERUIVEC2,        has_changed_buffer},
    {NGL_NODE_BUFFERUIVEC3,        has_changed_buffer},
    {NGL_NODE_BUFFERUIVEC4,        has_changed_buffer},
    {NGL_NODE_BUFFERMAT4,          has_changed_buffer},
    {NGL_NODE_ANIMATEDBUFFERFLOAT, has_changed_buffer},
    {NGL_NODE_ANIMATEDBUFFERVEC2,  has_changed_buffer},
    {NGL_NODE_ANIMATEDBUFFERVEC3,  has_changed_buffer},
    {NGL_NODE_ANIMATEDBUFFERVEC4,  has_changed_buffer},
    {NGL_NODE_UNIFORMFLOAT,        has_changed_uniform},
    {NGL_NODE_UNIFORMVEC2,         has_changed_uniform},
    {NGL_NODE_UNIFORMVEC3,         has_changed_uniform},
    {NGL_NODE_UNIFORMVEC4,         has_changed_uniform},
    {NGL_NODE_UNIFORMINT,          has_changed_uniform},
    {NGL_NODE_UNIFORMMAT4,         has_changed_uniform},
    {NGL_NODE_UNIFORMQUAT,         has_changed_uniform},
};

void ngli_block_update_data(struct block *block)
{
    //for (int i = 0; i < s->nb_fields; i++) {
    //    if (s->fields[i].need_upload)
    //}
}

void ngli_block_reset(struct block *block)
{
    ngli_free(block->fields);
    ngli_free(block->data);
}
