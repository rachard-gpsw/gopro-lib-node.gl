/*
 * Copyright 2016 GoPro Inc.
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
#include "format.h"
#include "glincludes.h"
#include "hmap.h"
#include "log.h"
#include "math_utils.h"
#include "memory.h"
#include "nodegl.h"
#include "nodes.h"
#include "spirv.h"
#include "utils.h"

#define TEXTURES_TYPES_LIST (const int[]){NGL_NODE_TEXTURE2D,       \
                                          NGL_NODE_TEXTURE3D,       \
                                          -1}

#define PROGRAMS_TYPES_LIST (const int[]){NGL_NODE_PROGRAM,         \
                                          -1}

#define UNIFORMS_TYPES_LIST (const int[]){NGL_NODE_BUFFERFLOAT,     \
                                          NGL_NODE_BUFFERVEC2,      \
                                          NGL_NODE_BUFFERVEC3,      \
                                          NGL_NODE_BUFFERVEC4,      \
                                          NGL_NODE_UNIFORMFLOAT,    \
                                          NGL_NODE_UNIFORMVEC2,     \
                                          NGL_NODE_UNIFORMVEC3,     \
                                          NGL_NODE_UNIFORMVEC4,     \
                                          NGL_NODE_UNIFORMQUAT,     \
                                          NGL_NODE_UNIFORMINT,      \
                                          NGL_NODE_UNIFORMMAT4,     \
                                          -1}

#define ATTRIBUTES_TYPES_LIST (const int[]){NGL_NODE_BUFFERFLOAT,   \
                                            NGL_NODE_BUFFERVEC2,    \
                                            NGL_NODE_BUFFERVEC3,    \
                                            NGL_NODE_BUFFERVEC4,    \
                                            -1}

#define GEOMETRY_TYPES_LIST (const int[]){NGL_NODE_CIRCLE,          \
                                          NGL_NODE_GEOMETRY,        \
                                          NGL_NODE_QUAD,            \
                                          NGL_NODE_TRIANGLE,        \
                                          -1}

#define BUFFERS_TYPES_LIST (const int[]){NGL_NODE_BUFFERFLOAT,      \
                                         NGL_NODE_BUFFERVEC2,       \
                                         NGL_NODE_BUFFERVEC3,       \
                                         NGL_NODE_BUFFERVEC4,       \
                                         NGL_NODE_BUFFERINT,        \
                                         NGL_NODE_BUFFERIVEC2,      \
                                         NGL_NODE_BUFFERIVEC3,      \
                                         NGL_NODE_BUFFERIVEC4,      \
                                         NGL_NODE_BUFFERUINT,       \
                                         NGL_NODE_BUFFERUIVEC2,     \
                                         NGL_NODE_BUFFERUIVEC3,     \
                                         NGL_NODE_BUFFERUIVEC4,     \
                                         -1}

#define OFFSET(x) offsetof(struct render_priv, x)
static const struct node_param render_params[] = {
    {"geometry", PARAM_TYPE_NODE, OFFSET(geometry), .flags=PARAM_FLAG_CONSTRUCTOR,
                 .node_types=GEOMETRY_TYPES_LIST,
                 .desc=NGLI_DOCSTRING("geometry to be rasterized")},
    {"program",  PARAM_TYPE_NODE, OFFSET(pipeline.program),
                 .node_types=PROGRAMS_TYPES_LIST,
                 .desc=NGLI_DOCSTRING("program to be executed")},
    {"textures", PARAM_TYPE_NODEDICT, OFFSET(pipeline.textures),
                 .node_types=TEXTURES_TYPES_LIST,
                 .desc=NGLI_DOCSTRING("textures made accessible to the `program`")},
    {"uniforms", PARAM_TYPE_NODEDICT, OFFSET(pipeline.uniforms),
                 .node_types=UNIFORMS_TYPES_LIST,
                 .desc=NGLI_DOCSTRING("uniforms made accessible to the `program`")},
    {"buffers",  PARAM_TYPE_NODEDICT, OFFSET(pipeline.buffers),
                 .node_types=BUFFERS_TYPES_LIST,
                 .desc=NGLI_DOCSTRING("buffers made accessible to the `program`")},
    {"attributes", PARAM_TYPE_NODEDICT, OFFSET(attributes),
                 .node_types=ATTRIBUTES_TYPES_LIST,
                 .desc=NGLI_DOCSTRING("extra vertex attributes made accessible to the `program`")},
    {"instance_attributes", PARAM_TYPE_NODEDICT, OFFSET(instance_attributes),
                 .node_types=ATTRIBUTES_TYPES_LIST,
                 .desc=NGLI_DOCSTRING("per instance extra vertex attributes made accessible to the `program`")},
    {"nb_instances", PARAM_TYPE_INT, OFFSET(nb_instances),
                 .desc=NGLI_DOCSTRING("number of instances to draw")},
    {NULL}
};

#ifndef VULKAN_BACKEND
static int update_geometry_uniforms(struct ngl_node *node)
{
    struct ngl_ctx *ctx = node->ctx;
    struct glcontext *gl = ctx->glcontext;
    struct render_priv *s = node->priv_data;

    const float *modelview_matrix = ngli_darray_tail(&ctx->modelview_matrix_stack);
    const float *projection_matrix = ngli_darray_tail(&ctx->projection_matrix_stack);

    if (s->modelview_matrix_location >= 0) {
        ngli_glUniformMatrix4fv(gl, s->modelview_matrix_location, 1, GL_FALSE, modelview_matrix);
    }

    if (s->projection_matrix_location >= 0) {
        ngli_glUniformMatrix4fv(gl, s->projection_matrix_location, 1, GL_FALSE, projection_matrix);
    }

    if (s->normal_matrix_location >= 0) {
        float normal_matrix[3*3];
        ngli_mat3_from_mat4(normal_matrix, modelview_matrix);
        ngli_mat3_inverse(normal_matrix, normal_matrix);
        ngli_mat3_transpose(normal_matrix, normal_matrix);
        ngli_glUniformMatrix3fv(gl, s->normal_matrix_location, 1, GL_FALSE, normal_matrix);
    }

    return 0;
}

static void update_vertex_attribs_from_pairs(struct glcontext *gl,
                                             const struct darray *attribute_pairs,
                                             int is_instance_attrib)
{
    const struct nodeprograminfopair *pairs = ngli_darray_data(attribute_pairs);
    for (int i = 0; i < ngli_darray_count(attribute_pairs); i++) {
        const struct nodeprograminfopair *pair = &pairs[i];
        const struct attributeprograminfo *info = pair->program_info;
        const GLint aid = info->location;
        struct buffer_priv *buffer = pair->node->priv_data;

        ngli_glEnableVertexAttribArray(gl, aid);
        ngli_glBindBuffer(gl, GL_ARRAY_BUFFER, buffer->buffer.id);
        ngli_glVertexAttribPointer(gl, aid, buffer->data_comp, GL_FLOAT, GL_FALSE, buffer->data_stride, NULL);

        if (is_instance_attrib)
            ngli_glVertexAttribDivisor(gl, aid, 1);
    }
}
#endif

#ifdef VULKAN_BACKEND
static VkResult create_graphics_pipeline(struct ngl_node *node, VkPipeline *pipeline_dstp)
{
    struct ngl_ctx *ctx = node->ctx;
    struct glcontext *vk = ctx->glcontext;
    struct render_priv *s = node->priv_data;

    struct pipeline *pipeline = &s->pipeline;
    const struct program_priv *program = pipeline->program->priv_data;

    /* Vertex input state */
    VkPipelineVertexInputStateCreateInfo vertex_input_state_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = s->nb_binds,
        .pVertexBindingDescriptions = s->nb_binds ? s->bind_descs : NULL,
        .vertexAttributeDescriptionCount = s->nb_binds,
        .pVertexAttributeDescriptions = s->nb_binds ? s->attr_descs : NULL,
    };

    struct geometry_priv *geometry = s->geometry->priv_data;

    /* Input Assembly State */
    VkPipelineInputAssemblyStateCreateInfo input_assembly_state_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = geometry->topology,
    };

    /* Viewport */
    VkViewport viewport = {
        .width = vk->config.width,
        .height = vk->config.height,
        .minDepth = 0.f,
        .maxDepth = 1.f,
    };

    VkRect2D scissor = {
        .extent = vk->extent,
    };

    VkPipelineViewportStateCreateInfo viewport_state_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor,
    };

    /* Rasterization */
    VkPipelineRasterizationStateCreateInfo rasterization_state_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .lineWidth = 1.f,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
    };

    /* Multisampling */
    VkPipelineMultisampleStateCreateInfo multisampling_state_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    /* Depth & stencil */
    // XXX

    /* Blend */
    const struct glstate *vkstate = &ctx->glstate;
    VkPipelineColorBlendAttachmentState colorblend_attachment_state = {
        .blendEnable = vkstate->blend,
        .srcColorBlendFactor = vkstate->blend_src_factor,
        .dstColorBlendFactor = vkstate->blend_dst_factor,
        .colorBlendOp = vkstate->blend_op,
        .srcAlphaBlendFactor = vkstate->blend_src_factor_a,
        .dstAlphaBlendFactor = vkstate->blend_dst_factor_a,
        .alphaBlendOp = vkstate->blend_op_a,
        .colorWriteMask = vkstate->color_write_mask,
    };

    VkPipelineColorBlendStateCreateInfo colorblend_state_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &colorblend_attachment_state,
    };

    /* Dynamic states */
    VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
    };

    VkPipelineDynamicStateCreateInfo dynamic_state_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = NGLI_ARRAY_NB(dynamic_states),
        .pDynamicStates = dynamic_states,

    };

    VkGraphicsPipelineCreateInfo graphics_pipeline_create_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = NGLI_ARRAY_NB(program->shader_stage_create_info),
        .pStages = program->shader_stage_create_info,
        .pVertexInputState = &vertex_input_state_create_info,
        .pInputAssemblyState = &input_assembly_state_create_info,
        .pViewportState = &viewport_state_create_info,
        .pRasterizationState = &rasterization_state_create_info,
        .pMultisampleState = &multisampling_state_create_info,
        .pDepthStencilState = NULL,
        .pColorBlendState = &colorblend_state_create_info,
        .pDynamicState = &dynamic_state_create_info,
        .layout = pipeline->pipeline_layout,
        .renderPass = vk->render_pass,
        .subpass = 0,
    };

    return vkCreateGraphicsPipelines(vk->device, NULL, 1,
                                     &graphics_pipeline_create_info,
                                     NULL, pipeline_dstp);
}

static int create_vertex_input_attrib_desc(struct ngl_node *node, const struct darray *attribute_pairs, int instance)
{
    struct render_priv *s = node->priv_data;
    struct ngl_ctx *ctx = node->ctx;
    struct glcontext *vk = ctx->glcontext;

    const struct nodeprograminfopair *pairs = ngli_darray_data(attribute_pairs);
    const int nb_pairs = ngli_darray_count(attribute_pairs);
    for (int i = 0; i < nb_pairs; i++) {
        const struct nodeprograminfopair *pair = &pairs[i];
        const struct spirv_variable *info = pair->program_info;
        struct buffer_priv *buffer = pair->node->priv_data;
        struct buffer *graphic_buffer = &buffer->buffer;

        VkVertexInputBindingDescription bind_desc = {
            .binding = s->nb_binds,
            .stride = buffer->data_stride,
            .inputRate = instance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX,
        };

        VkFormat data_format = VK_FORMAT_UNDEFINED;
        int ret = ngli_format_get_vk_format(vk, buffer->data_format, &data_format);
        if (ret < 0)
            return ret;

        VkVertexInputAttributeDescription attr_desc = {
            .binding = s->nb_binds,
            .location = info->offset,
            .format = data_format,
        };

        s->bind_descs[s->nb_binds] = bind_desc;
        s->attr_descs[s->nb_binds] = attr_desc;
        s->vkbufs[s->nb_binds] = graphic_buffer->vkbuf;
        s->nb_binds++;
    }

    return 0;
}

static int init_vertex_input_attrib_desc(struct ngl_node *node)
{
    struct render_priv *s = node->priv_data;

    const int nb_builtin_attribute_pairs = ngli_darray_count(&s->builtin_attribute_pairs);
    const int nb_attribute_pairs = ngli_darray_count(&s->attribute_pairs);
    const int nb_instance_attribute_pairs = ngli_darray_count(&s->instance_attribute_pairs);
    const int nb_attributes = nb_builtin_attribute_pairs + nb_attribute_pairs + nb_instance_attribute_pairs;

    s->bind_descs     = ngli_calloc(nb_attributes, sizeof(*s->bind_descs));
    s->attr_descs     = ngli_calloc(nb_attributes, sizeof(*s->attr_descs));
    s->vkbufs         = ngli_calloc(nb_attributes, sizeof(*s->vkbufs));
    s->vkbufs_offsets = ngli_calloc(nb_attributes, sizeof(*s->vkbufs_offsets));
    if (!s->bind_descs || !s->attr_descs || !s->vkbufs || !s->vkbufs_offsets)
        return -1;


    int ret = create_vertex_input_attrib_desc(node, &s->builtin_attribute_pairs, 0);
    if (ret < 0)
        return ret;

    ret = create_vertex_input_attrib_desc(node, &s->attribute_pairs, 0);
    if (ret < 0)
        return ret;

    ret = create_vertex_input_attrib_desc(node, &s->instance_attribute_pairs, 1);
    if (ret < 0)
        return ret;

    return 0;
}
#else
static void update_vertex_attribs(struct ngl_node *node)
{
    struct ngl_ctx *ctx = node->ctx;
    struct glcontext *gl = ctx->glcontext;
    struct render_priv *s = node->priv_data;

    update_vertex_attribs_from_pairs(gl, &s->builtin_attribute_pairs, 0);
    update_vertex_attribs_from_pairs(gl, &s->attribute_pairs, 0);
    update_vertex_attribs_from_pairs(gl, &s->instance_attribute_pairs, 1);
}

static void disable_vertex_attribs_from_pairs(struct glcontext *gl,
                                              const struct darray *attribute_pairs)
{
    const struct nodeprograminfopair *pairs = ngli_darray_data(attribute_pairs);
    for (int i = 0; i < ngli_darray_count(attribute_pairs); i++) {
        const struct nodeprograminfopair *pair = &pairs[i];
        const struct attributeprograminfo *info = pair->program_info;
        ngli_glDisableVertexAttribArray(gl, info->location);
    }
}

static void disable_vertex_attribs(struct ngl_node *node)
{
    struct ngl_ctx *ctx = node->ctx;
    struct glcontext *gl = ctx->glcontext;
    struct render_priv *s = node->priv_data;

    disable_vertex_attribs_from_pairs(gl, &s->builtin_attribute_pairs);
    disable_vertex_attribs_from_pairs(gl, &s->attribute_pairs);
    disable_vertex_attribs_from_pairs(gl, &s->instance_attribute_pairs);
}

static int get_uniform_location(struct hmap *uniforms, const char *name)
{
    const struct uniformprograminfo *info = ngli_hmap_get(uniforms, name);
    return info ? info->location : -1;
}
#endif

static int pair_node_to_attribinfo(struct render_priv *s,
                                   struct darray *attribute_pairs,
                                   const char *name,
                                   struct ngl_node *anode)
{
    const struct ngl_node *pnode = s->pipeline.program;
    const struct program_priv *program = pnode->priv_data;

#ifdef VULKAN_BACKEND
    const struct spirv_variable *active_attribute =
        ngli_hmap_get(program->vert_desc->attributes, name);
#else
    const struct attributeprograminfo *active_attribute =
        ngli_hmap_get(program->active_attributes, name);
#endif

    if (!active_attribute)
        return 1;

#ifndef VULKAN_BACKEND // XXX
    if (active_attribute->location < 0)
        return 0;
#endif

    int ret = ngli_node_buffer_ref(anode);
    if (ret < 0)
        return ret;

    struct nodeprograminfopair pair = {
        .node = anode,
        .program_info = (void *)active_attribute,
    };
    snprintf(pair.name, sizeof(pair.name), "%s", name);
    if (!ngli_darray_push(attribute_pairs, &pair)) {
        ngli_node_buffer_unref(anode);
        return -1;
    }

    return 0;
}

static int pair_nodes_to_attribinfo(struct ngl_node *node,
                                    struct darray *attribute_pairs,
                                    struct hmap *attributes,
                                    int per_instance,
                                    int warn_not_found)
{
    if (!attributes)
        return 0;

    struct render_priv *s = node->priv_data;

    const struct hmap_entry *entry = NULL;
    while ((entry = ngli_hmap_next(attributes, entry))) {
        struct ngl_node *anode = entry->data;
        struct buffer_priv *buffer = anode->priv_data;

        if (per_instance) {
            if (buffer->count != s->nb_instances) {
                LOG(ERROR,
                    "attribute buffer %s count (%d) does not match instance count (%d)",
                    entry->key,
                    buffer->count,
                    s->nb_instances);
                return -1;
            }
        } else {
            struct geometry_priv *geometry = s->geometry->priv_data;
            struct buffer_priv *vertices = geometry->vertices_buffer->priv_data;
            if (buffer->count != vertices->count) {
                LOG(ERROR,
                    "attribute buffer %s count (%d) does not match vertices count (%d)",
                    entry->key,
                    buffer->count,
                    vertices->count);
                return -1;
            }
        }

        int ret = pair_node_to_attribinfo(s, attribute_pairs, entry->key, anode);
        if (ret < 0)
            return ret;

        if (warn_not_found && ret == 1) {
            const struct ngl_node *pnode = s->pipeline.program;
            LOG(WARNING, "attribute %s attached to %s not found in %s",
                entry->key, node->label, pnode->label);
        }
    }
    return 0;
}

#ifndef VULKAN_BACKEND
static void draw_elements(struct glcontext *gl, struct render_priv *render)
{
    struct geometry_priv *geometry = render->geometry->priv_data;
    const struct buffer_priv *indices = geometry->indices_buffer->priv_data;
    ngli_glBindBuffer(gl, GL_ELEMENT_ARRAY_BUFFER, indices->buffer.id);
    ngli_glDrawElements(gl, geometry->topology, indices->count, render->indices_type, 0);
}

static void draw_elements_instanced(struct glcontext *gl, struct render_priv *render)
{
    struct geometry_priv *geometry = render->geometry->priv_data;
    struct buffer_priv *indices = geometry->indices_buffer->priv_data;
    ngli_glBindBuffer(gl, GL_ELEMENT_ARRAY_BUFFER, indices->buffer.id);
    ngli_glDrawElementsInstanced(gl, geometry->topology, indices->count, render->indices_type, 0, render->nb_instances);
}

static void draw_arrays(struct glcontext *gl, struct render_priv *render)
{
    struct geometry_priv *geometry = render->geometry->priv_data;
    struct buffer_priv *vertices = geometry->vertices_buffer->priv_data;
    ngli_glDrawArrays(gl, geometry->topology, 0, vertices->count);
}

static void draw_arrays_instanced(struct glcontext *gl, struct render_priv *render)
{
    struct geometry_priv *geometry = render->geometry->priv_data;
    struct buffer_priv *vertices = geometry->vertices_buffer->priv_data;
}
#else
static void draw_elements(VkCommandBuffer cmd_buf, struct render_priv *render)
{
    struct geometry_priv *geometry = render->geometry->priv_data;
    const struct buffer_priv *indices = geometry->indices_buffer->priv_data;
    vkCmdBindIndexBuffer(cmd_buf, indices->buffer.vkbuf, 0, render->indices_type);
    vkCmdDrawIndexed(cmd_buf, indices->count, 1, 0, 0, 0);
}

static void draw_elements_instanced(VkCommandBuffer cmd_buf, struct render_priv *render)
{
    struct geometry_priv *geometry = render->geometry->priv_data;
    struct buffer_priv *indices = geometry->indices_buffer->priv_data;
    vkCmdBindIndexBuffer(cmd_buf, indices->buffer.vkbuf, 0, render->indices_type);
    vkCmdDrawIndexed(cmd_buf, indices->count, render->nb_instances, 0, 0, 0);
}

static void draw_arrays(VkCommandBuffer cmd_buf, struct render_priv *render)
{
    struct geometry_priv *geometry = render->geometry->priv_data;
    struct buffer_priv *vertices = geometry->vertices_buffer->priv_data;
    vkCmdDraw(cmd_buf, vertices->count, 1, 0, 0);
}

static void draw_arrays_instanced(VkCommandBuffer cmd_buf, struct render_priv *render)
{
    struct geometry_priv *geometry = render->geometry->priv_data;
    struct buffer_priv *vertices = geometry->vertices_buffer->priv_data;
    vkCmdDraw(cmd_buf, vertices->count, render->nb_instances, 0, 0);
}
#endif

#define GEOMETRY_OFFSET(x) offsetof(struct geometry_priv, x)
static const struct {
    const char *const_name;
    int offset;
} attrib_const_map[] = {
    {"ngl_position", GEOMETRY_OFFSET(vertices_buffer)},
    {"ngl_uvcoord",  GEOMETRY_OFFSET(uvcoords_buffer)},
    {"ngl_normal",   GEOMETRY_OFFSET(normals_buffer)},
};

static int init_builtin_attributes(struct render_priv *s)
{
    s->builtin_attributes = ngli_hmap_create();
    if (!s->builtin_attributes)
        return -1;

    struct geometry_priv *geometry = s->geometry->priv_data;
    for (int i = 0; i < NGLI_ARRAY_NB(attrib_const_map); i++) {
        const int offset = attrib_const_map[i].offset;
        const char *const_name = attrib_const_map[i].const_name;
        uint8_t *buffer_node_p = ((uint8_t *)geometry) + offset;
        struct ngl_node *anode = *(struct ngl_node **)buffer_node_p;
        if (!anode)
            continue;

        int ret = ngli_hmap_set(s->builtin_attributes, const_name, anode);
        if (ret < 0)
            return ret;
    }
    return 0;
}

static int render_init(struct ngl_node *node)
{
    struct ngl_ctx *ctx = node->ctx;
    struct render_priv *s = node->priv_data;

    if (!s->pipeline.program) {
        s->pipeline.program = ngl_node_create(NGL_NODE_PROGRAM);
        if (!s->pipeline.program)
            return -1;
        int ret = ngli_node_attach_ctx(s->pipeline.program, ctx);
        if (ret < 0)
            return ret;
    }

#ifdef VULKAN_BACKEND
    struct glcontext *vk = ctx->glcontext;
    s->pipeline.create_func = create_graphics_pipeline;
    s->pipeline.queue_family_id = vk->queue_family_graphics_id;
#endif

    int ret = ngli_pipeline_init(node);
    if (ret < 0)
        return ret;

#ifndef VULKAN_BACKEND
    /* Instancing checks */
    struct glcontext *gl = ctx->glcontext;

    if (s->nb_instances && !(gl->features & NGLI_FEATURE_DRAW_INSTANCED)) {
        LOG(ERROR, "context does not support instanced draws");
        return -1;
    }

    if (s->instance_attributes && !(gl->features & NGLI_FEATURE_INSTANCED_ARRAY)) {
        LOG(ERROR, "context does not support instanced arrays");
        return -1;
    }
#endif

    /* Builtin uniforms */
#ifdef VULKAN_BACKEND
    // FIXME: modelview/projection are handled differently (constants), but
    // normals need to be handled
#else
    struct ngl_node *pnode = s->pipeline.program;
    struct program_priv *program = pnode->priv_data;
    struct hmap *uniforms = program->active_uniforms;

    s->modelview_matrix_location  = get_uniform_location(uniforms, "ngl_modelview_matrix");
    s->projection_matrix_location = get_uniform_location(uniforms, "ngl_projection_matrix");
    s->normal_matrix_location     = get_uniform_location(uniforms, "ngl_normal_matrix");
#endif

    /* User and builtin attribute pairs */
    ngli_darray_init(&s->builtin_attribute_pairs, sizeof(struct nodeprograminfopair), 0);
    ngli_darray_init(&s->attribute_pairs, sizeof(struct nodeprograminfopair), 0);
    ngli_darray_init(&s->instance_attribute_pairs, sizeof(struct nodeprograminfopair), 0);

    ret = init_builtin_attributes(s);
    if (ret < 0)
        return ret;

    /* Builtin vertex attributes */
    ret = pair_nodes_to_attribinfo(node, &s->builtin_attribute_pairs, s->builtin_attributes, 0, 0);
    if (ret < 0)
        return ret;

    /* User vertex attributes */
    ret = pair_nodes_to_attribinfo(node, &s->attribute_pairs, s->attributes, 0, 1);
    if (ret < 0)
        return ret;

    /* User per instance vertex attributes */
    ret = pair_nodes_to_attribinfo(node, &s->instance_attribute_pairs, s->instance_attributes, 1, 1);
    if (ret < 0)
        return ret;

    struct geometry_priv *geometry = s->geometry->priv_data;
    if (geometry->indices_buffer) {
        ret = ngli_node_buffer_ref(geometry->indices_buffer);
        if (ret < 0)
            return ret;
        s->has_indices_buffer_ref = 1;

#ifdef VULKAN_BACKEND
        const int index_node_type = geometry->indices_buffer->class->id;
        ngli_assert(index_node_type == NGL_NODE_BUFFERUSHORT ||
                    index_node_type == NGL_NODE_BUFFERUINT);
        s->indices_type = index_node_type == NGL_NODE_BUFFERUINT ? VK_INDEX_TYPE_UINT32
                                                                 : VK_INDEX_TYPE_UINT16;
#else
        struct buffer_priv *indices = geometry->indices_buffer->priv_data;
        ngli_format_get_gl_texture_format(gl, indices->data_format, NULL, NULL, &s->indices_type);
#endif
    }

#ifdef VULKAN_BACKEND
    ret = init_vertex_input_attrib_desc(node);
    if (ret < 0)
        return ret;
#else
    if (gl->features & NGLI_FEATURE_VERTEX_ARRAY_OBJECT) {
        ngli_glGenVertexArrays(gl, 1, &s->vao_id);
        ngli_glBindVertexArray(gl, s->vao_id);
        update_vertex_attribs(node);
    }
#endif

    if (geometry->indices_buffer)
        s->draw = s->nb_instances > 0 ? draw_elements_instanced : draw_elements;
    else
        s->draw = s->nb_instances > 0 ? draw_arrays_instanced : draw_arrays;

    return 0;
}

static void uninit_attributes(struct darray *attribute_pairs)
{
    struct nodeprograminfopair *pairs = ngli_darray_data(attribute_pairs);
    for (int i = 0; i < ngli_darray_count(attribute_pairs); i++) {
        struct nodeprograminfopair *pair = &pairs[i];
        ngli_node_buffer_unref(pair->node);
    }

    ngli_darray_reset(attribute_pairs);
}

static void render_uninit(struct ngl_node *node)
{
    struct render_priv *s = node->priv_data;

#ifdef VULKAN_BACKEND
    ngli_free(s->bind_descs);
    ngli_free(s->attr_descs);
    ngli_free(s->vkbufs);
    ngli_free(s->vkbufs_offsets);
#else
    struct ngl_ctx *ctx = node->ctx;
    struct glcontext *gl = ctx->glcontext;

    ngli_hmap_freep(&s->builtin_attributes);

    if (gl->features & NGLI_FEATURE_VERTEX_ARRAY_OBJECT) {
        ngli_glDeleteVertexArrays(gl, 1, &s->vao_id);
    }
#endif

    ngli_pipeline_uninit(node);

    if (s->has_indices_buffer_ref) {
        struct geometry_priv *geometry = s->geometry->priv_data;
        ngli_node_buffer_unref(geometry->indices_buffer);
    }

    uninit_attributes(&s->builtin_attribute_pairs);
    uninit_attributes(&s->attribute_pairs);
    uninit_attributes(&s->instance_attribute_pairs);
}

static int update_attributes(struct darray *attribute_pairs, double t)
{
    struct nodeprograminfopair *pairs = ngli_darray_data(attribute_pairs);
    for (int i = 0; i < ngli_darray_count(attribute_pairs); i++) {
        struct nodeprograminfopair *pair = &pairs[i];
        struct ngl_node *bnode = pair->node;
        int ret = ngli_node_update(bnode, t);
        if (ret < 0)
            return ret;
        ret = ngli_node_buffer_upload(bnode);
        if (ret < 0)
            return ret;
    }
    return 0;
}

static int render_update(struct ngl_node *node, double t)
{
    struct render_priv *s = node->priv_data;

    int ret = ngli_node_update(s->geometry, t);
    if (ret < 0)
        return ret;

    ret = update_attributes(&s->builtin_attribute_pairs, t);
    if (ret < 0)
        return ret;

    ret = update_attributes(&s->attribute_pairs, t);
    if (ret < 0)
        return ret;

    ret = update_attributes(&s->instance_attribute_pairs, t);
    if (ret < 0)
        return ret;

    return ngli_pipeline_update(node, t);
}

static void render_draw(struct ngl_node *node)
{
    struct ngl_ctx *ctx = node->ctx;
    struct render_priv *s = node->priv_data;

#ifdef VULKAN_BACKEND
    struct glcontext *vk = ctx->glcontext;

    ngli_pipeline_upload_data(node);

    VkCommandBuffer cmd_buf = s->pipeline.command_buffers[vk->img_index];

    VkCommandBufferBeginInfo command_buffer_begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
    };

    VkResult ret = vkBeginCommandBuffer(cmd_buf, &command_buffer_begin_info);
    if (ret != VK_SUCCESS)
        return;


    const float *rgba = vk->config.clear_color;
    VkClearValue clear_color = {.color.float32={rgba[0], rgba[1], rgba[2], rgba[3]}};
    VkRenderPassBeginInfo render_pass_begin_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = vk->current_render_pass,
        .framebuffer = vk->current_framebuffer,
        .renderArea = {
            .extent = {
                vk->config.width,
                vk->config.height,
            },
        },
        .clearValueCount = 1,
        .pClearValues = &clear_color,
    };

    VkViewport viewport = {
        .width = vk->config.width,
        .height = vk->config.height,
        .minDepth = 0.f,
        .maxDepth = 1.f,
    };
    LOG(ERROR, "w=%d h=%d", vk->config.width);
    vkCmdSetViewport(cmd_buf, 0, 1, &viewport);
    vkCmdBeginRenderPass(cmd_buf, &render_pass_begin_info,
                         VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, s->pipeline.vkpipeline);

    vkCmdBindVertexBuffers(cmd_buf, 0, s->nb_binds, s->vkbufs, s->vkbufs_offsets);

    const size_t matrix_size = 4 * 4 * sizeof(float);
    const float *modelview_matrix = ngli_darray_tail(&ctx->modelview_matrix_stack);
    const float *projection_matrix = ngli_darray_tail(&ctx->projection_matrix_stack);

    vkCmdPushConstants(cmd_buf, s->pipeline.pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, matrix_size,
                       modelview_matrix);
    vkCmdPushConstants(cmd_buf, s->pipeline.pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT, matrix_size, matrix_size,
                       projection_matrix);

    // TODO: should handle all type of buffer in different stages
    if (s->pipeline.descriptor_sets) {
        vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, s->pipeline.pipeline_layout,
                                0, 1, &s->pipeline.descriptor_sets[vk->img_index], 0, NULL);
    }

    s->draw(cmd_buf, s);

    vkCmdEndRenderPass(cmd_buf);

    ret = vkEndCommandBuffer(cmd_buf);
    if (ret != VK_SUCCESS)
        return;

    vk->command_buffers[vk->nb_command_buffers++] = cmd_buf;
#else
    struct glcontext *gl = ctx->glcontext;

    const struct program_priv *program = s->pipeline.program->priv_data;

    ngli_glUseProgram(gl, program->program_id);

    if (gl->features & NGLI_FEATURE_VERTEX_ARRAY_OBJECT) {
        ngli_glBindVertexArray(gl, s->vao_id);
    } else {
        update_vertex_attribs(node);
    }

    update_geometry_uniforms(node);

    int ret = ngli_pipeline_upload_data(node);
    if (ret < 0) {
        LOG(ERROR, "pipeline upload data error");
    }

    s->draw(gl, s);

    if (!(gl->features & NGLI_FEATURE_VERTEX_ARRAY_OBJECT)) {
        disable_vertex_attribs(node);
    }
#endif
}

const struct node_class ngli_render_class = {
    .id        = NGL_NODE_RENDER,
    .name      = "Render",
    .init      = render_init,
    .uninit    = render_uninit,
    .update    = render_update,
    .draw      = render_draw,
    .priv_size = sizeof(struct render_priv),
    .params    = render_params,
    .file      = __FILE__,
};
