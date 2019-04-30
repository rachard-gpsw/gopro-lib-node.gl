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

#include <string.h>
#include <stddef.h>

#include "block.h"
#include "buffer.h"
#include "log.h"
#include "memory.h"
#include "nodegl.h"
#include "nodes.h"

static const struct param_choices layout_choices = {
    .name = "memory_layout",
    .consts = {
        {"std140", NGLI_BLOCK_LAYOUT_STD140, .desc=NGLI_DOCSTRING("standard uniform block memory layout 140")},
        {"std430", NGLI_BLOCK_LAYOUT_STD430, .desc=NGLI_DOCSTRING("standard uniform block memory layout 430")},
        {NULL}
    }
};

#define UNIFORMS_TYPES_LIST (const int[]){NGL_NODE_ANIMATEDBUFFERFLOAT, \
                                          NGL_NODE_ANIMATEDBUFFERVEC2,  \
                                          NGL_NODE_ANIMATEDBUFFERVEC3,  \
                                          NGL_NODE_ANIMATEDBUFFERVEC4,  \
                                          NGL_NODE_BUFFERFLOAT,         \
                                          NGL_NODE_BUFFERVEC2,          \
                                          NGL_NODE_BUFFERVEC3,          \
                                          NGL_NODE_BUFFERVEC4,          \
                                          NGL_NODE_BUFFERINT,           \
                                          NGL_NODE_BUFFERIVEC2,         \
                                          NGL_NODE_BUFFERIVEC3,         \
                                          NGL_NODE_BUFFERIVEC4,         \
                                          NGL_NODE_BUFFERUINT,          \
                                          NGL_NODE_BUFFERUIVEC2,        \
                                          NGL_NODE_BUFFERUIVEC3,        \
                                          NGL_NODE_BUFFERUIVEC4,        \
                                          NGL_NODE_BUFFERMAT4,          \
                                          NGL_NODE_UNIFORMFLOAT,        \
                                          NGL_NODE_UNIFORMVEC2,         \
                                          NGL_NODE_UNIFORMVEC3,         \
                                          NGL_NODE_UNIFORMVEC4,         \
                                          NGL_NODE_UNIFORMINT,          \
                                          NGL_NODE_UNIFORMMAT4,         \
                                          NGL_NODE_UNIFORMQUAT,         \
                                          -1}

#define OFFSET(x) offsetof(struct block_priv, x)
static const struct node_param block_params[] = {
    {"fields", PARAM_TYPE_NODELIST, OFFSET(fields),
               .node_types=UNIFORMS_TYPES_LIST,
               .desc=NGLI_DOCSTRING("block fields defined in the graphic program")},
    {"layout", PARAM_TYPE_SELECT, OFFSET(layout), {.i64=NGLI_BLOCK_LAYOUT_STD140},
               .choices=&layout_choices,
               .desc=NGLI_DOCSTRING("memory layout set in the graphic program")},
    {NULL}
};

int ngli_node_block_ref(struct ngl_node *node)
{
    struct ngl_ctx *ctx = node->ctx;
    struct glcontext *gl = ctx->glcontext;
    struct block_priv *s = node->priv_data;

    if (s->buffer_refcount++ == 0) {
        int ret = ngli_buffer_allocate(&s->buffer, gl, s->data_size, s->usage);
        if (ret < 0)
            return ret;

        ret = ngli_buffer_upload(&s->buffer, s->data, s->data_size);
        if (ret < 0)
            return ret;

        s->buffer_last_upload_time = -1.;
    }

    return 0;
}

void ngli_node_block_unref(struct ngl_node *node)
{
    struct block_priv *s = node->priv_data;

    ngli_assert(s->buffer_refcount);
    if (s->buffer_refcount-- == 1)
        ngli_buffer_free(&s->buffer);
}

int ngli_node_block_upload(struct ngl_node *node)
{
    struct block_priv *s = node->priv_data;

    if (s->has_changed && s->buffer_last_upload_time != node->last_update_time) {
        int ret = ngli_buffer_upload(&s->buffer, s->data, s->data_size);
        if (ret < 0)
            return ret;
        s->buffer_last_upload_time = node->last_update_time;
        s->has_changed = 0;
    }

    return 0;
}

static int has_changed_uniform(const struct ngl_node *unode)
{
    const struct uniform_priv *uniform = unode->priv_data;
    return uniform->dynamic || uniform->live_changed;
}

static int has_changed_buffer(const struct ngl_node *bnode)
{
    const struct buffer_priv *buffer = bnode->priv_data;
    return buffer->dynamic;
}

static const struct type_spec {
    int class_id;
    int (*has_changed)(const struct ngl_node *node);
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

static void update_data(uint8_t *dst, const struct variable *v,
                        const struct block_field_info *fi)
{
    if (!s->count || v->data_stride == fi->stride) {
        /* single value or array with the same stride */
        memcpy(dst, v->data, v->size);
    } else {
        /* array with a mismatching stride */
        for (int i = 0; i < v->count; i++)
            memcpy(dst + i * fi->stride, v->data + i * v->data_stride, v->data_stride);
    }
}

static void update_block_data(struct block_priv *s, int forced)
{
    for (int i = 0; i < s->nb_fields; i++) {
        const struct ngl_node *field_node = s->fields[i];
        const struct block_field_info *fi = &s->field_info[i];
        const struct type_spec *spec = &type_specs[fi->spec_id];
        if (!forced && !spec->has_changed(field_node))
            continue;
        update_data(s->data + fi->offset, &s->variables[i], fi);
        s->has_changed = 1; // TODO: only re-upload the changing data segments
    }
}

static void variable_from_uniform(struct variable *v, const struct uniform_priv *uniform)
{
    v->type        = uniform->variable_type;
    v->count       = 0;
    v->data        = uniform->data;
    v->data_stride = 0;
    v->need_upload = 1;
}

static void variable_from_buffer(struct variable *v, const struct buffer_priv *buffer)
{
    v->type        = buffer->variable_type;
    v->count       = buffer->count;
    v->data        = buffer->data;
    v->data_stride = buffer->data_stride;
    v->need_upload = 1;
}

static void variable_from_node(struct variable *v, const struct ngl_node *unode)
{
    switch (unode->class->id) {
        case NGL_NODE_BUFFERFLOAT:
        case NGL_NODE_BUFFERVEC2:
        case NGL_NODE_BUFFERVEC3:
        case NGL_NODE_BUFFERVEC4:
        case NGL_NODE_BUFFERINT:
        case NGL_NODE_BUFFERIVEC2:
        case NGL_NODE_BUFFERIVEC3:
        case NGL_NODE_BUFFERIVEC4:
        case NGL_NODE_BUFFERUINT:
        case NGL_NODE_BUFFERUIVEC2:
        case NGL_NODE_BUFFERUIVEC3:
        case NGL_NODE_BUFFERUIVEC4:
        case NGL_NODE_BUFFERMAT4:
        case NGL_NODE_ANIMATEDBUFFERFLOAT:
        case NGL_NODE_ANIMATEDBUFFERVEC2:
        case NGL_NODE_ANIMATEDBUFFERVEC3:
        case NGL_NODE_ANIMATEDBUFFERVEC4:   return variable_from_buffer(v, unode->priv_data);
        case NGL_NODE_UNIFORMFLOAT:
        case NGL_NODE_UNIFORMVEC2:
        case NGL_NODE_UNIFORMVEC3:
        case NGL_NODE_UNIFORMVEC4:
        case NGL_NODE_UNIFORMINT:
        case NGL_NODE_UNIFORMMAT4:
        case NGL_NODE_UNIFORMQUAT:          return variable_from_uniform(v, unode->priv_data);
        default:
            ngli_assert(0);
    }
}

static int block_init(struct ngl_node *node)
{
    struct ngl_ctx *ctx = node->ctx;
    struct glcontext *gl = ctx->glcontext;
    struct block_priv *s = node->priv_data;

    ngli_block_init(&s->block, s->nb_fields, s->variables);

    s->variables = ngli_calloc(s->nb_fields, sizeof(*s->variables));
    if (!s->variables)
        return -1;

    for (int i = 0; i < s->nb_fields; i++)
        variable_from_node(&s->variables[i], s->fields[i]);

    int ret = ngli_block_init(block, s->variables);
    if (ret < 0)
        return ret;

    update_block_data(s, 1);
    return 0;
}

static int block_update(struct ngl_node *node, double t)
{
    struct block_priv *s = node->priv_data;

    for (int i = 0; i < s->nb_fields; i++) {
        struct ngl_node *field_node = s->fields[i];
        int ret = ngli_node_update(field_node, t);
        if (ret < 0)
            return ret;
    }

    update_block_data(s, 0);
    return 0;
}

static void block_uninit(struct ngl_node *node)
{
    struct block_priv *s = node->priv_data;

    ngli_block_reset(&s->block);

    ngli_free(s->field_info);
    ngli_free(s->data);
    ngli_free(s->variables);
}

const struct node_class ngli_block_class = {
    .id        = NGL_NODE_BLOCK,
    .name      = "Block",
    .init      = block_init,
    .update    = block_update,
    .uninit    = block_uninit,
    .priv_size = sizeof(struct block_priv),
    .params    = block_params,
    .file      = __FILE__,
};
