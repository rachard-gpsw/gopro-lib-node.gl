/*
 * Copyright 2018 GoPro Inc.
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

#include "fbo.h"
#include "format.h"
#include "glcontext.h"
#include "glincludes.h"
#include "log.h"
#include "texture.h"
#include "memory.h"

#ifdef VULKAN_BACKEND
static int is_depth_attachment(VkFormat format)
{
    switch (format) {
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return -1;
    default:
        return 0;
    }
}

#else
static GLenum get_gl_attachment_index(GLenum format)
{
    switch (format) {
    case GL_DEPTH_COMPONENT:
    case GL_DEPTH_COMPONENT16:
    case GL_DEPTH_COMPONENT24:
    case GL_DEPTH_COMPONENT32F:
        return GL_DEPTH_ATTACHMENT;
    case GL_DEPTH_STENCIL:
    case GL_DEPTH24_STENCIL8:
    case GL_DEPTH32F_STENCIL8:
        return GL_DEPTH_STENCIL_ATTACHMENT;
    case GL_STENCIL_INDEX:
    case GL_STENCIL_INDEX8:
        return GL_STENCIL_ATTACHMENT;
    default:
        return GL_COLOR_ATTACHMENT0;
    }
}

static const GLenum depth_stencil_attachments[] = {GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT};
#endif

int ngli_fbo_init(struct fbo *fbo, struct glcontext *gl, const struct fbo_params *params)
{
    fbo->gl = gl;
    fbo->width = params->width;
    fbo->height = params->height;
#ifdef VULKAN_BACKEND
    struct fbo *s = fbo;
    struct glcontext *vk = gl;

    VkAttachmentDescription *attachment_descriptions = ngli_calloc(params->nb_attachments, sizeof(*attachment_descriptions));
    if (!attachment_descriptions)
        return -1;

    VkAttachmentReference *color_attachments = ngli_calloc(params->nb_attachments, sizeof(*color_attachments));
    if (!color_attachments)
        return -1;
    int nb_color_attachments = 0;
    int nb_depth_attachments = 0;
    VkAttachmentReference depth_attachment = {0};

    for (int i = 0; i < params->nb_attachments; i++) {
        const struct texture *texture = params->attachments[i];
        VkAttachmentDescription *desc = &attachment_descriptions[i];
        VkFormat format;
        ngli_format_get_vk_format(vk, texture->params.format, &format);
        desc->format = format;
        desc->samples = VK_SAMPLE_COUNT_1_BIT;
        desc->loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        desc->storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        desc->stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        desc->stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        desc->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        desc->finalLayout = VK_IMAGE_LAYOUT_GENERAL;

        if (is_depth_attachment(texture->format)) {
            depth_attachment.attachment = i;
            depth_attachment.layout = texture->image_layout;
            nb_depth_attachments++;
        } else {
            color_attachments[nb_color_attachments].attachment = i;
            color_attachments[nb_color_attachments].layout = texture->image_layout;
            nb_color_attachments++;
        }
    }
    /*
    VkAttachmentReference color_reference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
    VkAttachmentReference depth_reference = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
    */

    VkSubpassDescription subpass_description = {
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = nb_color_attachments,
        .pColorAttachments = color_attachments,
    };

    if (nb_depth_attachments > 0)
        subpass_description.pDepthStencilAttachment = &depth_attachment;

    VkSubpassDependency dependencies[2] = {0};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo render_pass_create_info = {};
    render_pass_create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_create_info.attachmentCount = params->nb_attachments;
    render_pass_create_info.pAttachments = attachment_descriptions;
    render_pass_create_info.subpassCount = 1;
    render_pass_create_info.pSubpasses = &subpass_description;
    render_pass_create_info.dependencyCount = NGLI_ARRAY_NB(dependencies);
    render_pass_create_info.pDependencies = dependencies;

    VkResult res = vkCreateRenderPass(vk->device, &render_pass_create_info, NULL, &s->render_pass);
    if (res != VK_SUCCESS)
        return -1;

    VkImageView *attachments = ngli_calloc(params->nb_attachments, sizeof(*attachments));
    if (!attachments)
        return -1;
    LOG(ERROR, "=%d", params->nb_attachments);
    for (int i = 0; i < params->nb_attachments; i++) {
        struct texture *texture = params->attachments[i];
        attachments[i] = texture->image_view;
        LOG(ERROR, "i=%d %p", i, texture->image_view);
    }

    VkFramebufferCreateInfo framebuffer_create_info = {
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = s->render_pass,
        .attachmentCount = params->nb_attachments,
        .pAttachments = attachments,
        .width = s->width,
        .height = s->height,
        .layers = 1
    };

    res = vkCreateFramebuffer(vk->device, &framebuffer_create_info, NULL, &s->framebuffer);
    if (res != VK_SUCCESS)
        return -1;
#else
    fbo->gl = gl;
    fbo->width = params->width;
    fbo->height = params->height;

    ngli_darray_init(&fbo->depth_indices, sizeof(GLenum), 0);

    GLuint fbo_id = 0;
    ngli_glGetIntegerv(gl, GL_FRAMEBUFFER_BINDING, (GLint *)&fbo_id);

    ngli_glGenFramebuffers(gl, 1, &fbo->id);
    ngli_glBindFramebuffer(gl, GL_FRAMEBUFFER, fbo->id);

    int color_index = 0;
    for (int i = 0; i < params->nb_attachments; i++) {
        const struct texture *attachment = params->attachments[i];

        GLenum attachment_index = get_gl_attachment_index(attachment->format);
        const int is_color_attachment = attachment_index == GL_COLOR_ATTACHMENT0;
        if (is_color_attachment) {
            if (color_index >= gl->max_color_attachments) {
                LOG(ERROR, "could not attach color buffer %d (maximum %d)",
                    color_index, gl->max_color_attachments);
                ngli_glBindFramebuffer(gl, GL_FRAMEBUFFER, fbo_id);
                return -1;
            }
            attachment_index = attachment_index + color_index++;
        }

        switch (attachment->target) {
        case GL_RENDERBUFFER:
            if (gl->backend == NGL_BACKEND_OPENGLES && gl->version < 300 && attachment_index == GL_DEPTH_STENCIL_ATTACHMENT) {
                ngli_glFramebufferRenderbuffer(gl, GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, attachment->id);
                ngli_glFramebufferRenderbuffer(gl, GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, attachment->id);
                ngli_darray_push(&fbo->depth_indices, depth_stencil_attachments);
                ngli_darray_push(&fbo->depth_indices, depth_stencil_attachments + 1);
            } else {
                ngli_glFramebufferRenderbuffer(gl, GL_FRAMEBUFFER, attachment_index, GL_RENDERBUFFER, attachment->id);
                if (!is_color_attachment) {
                    if (gl->platform == NGL_PLATFORM_IOS && attachment_index == GL_DEPTH_STENCIL_ATTACHMENT) {
                        ngli_darray_push(&fbo->depth_indices, depth_stencil_attachments);
                        ngli_darray_push(&fbo->depth_indices, depth_stencil_attachments + 1);
                    } else {
                        ngli_darray_push(&fbo->depth_indices, &attachment_index);
                    }
                }
            }
            break;
        case GL_TEXTURE_2D:
            ngli_glFramebufferTexture2D(gl, GL_FRAMEBUFFER, attachment_index, GL_TEXTURE_2D, attachment->id, 0);
            break;
        default:
            ngli_assert(0);
        }
    }

    if (ngli_glCheckFramebufferStatus(gl, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        LOG(ERROR, "framebuffer %u is not complete", fbo->id);
        ngli_glBindFramebuffer(gl, GL_FRAMEBUFFER, fbo_id);
        return -1;
    }

    ngli_glBindFramebuffer(gl, GL_FRAMEBUFFER, fbo_id);
#endif

    return 0;
}

int ngli_fbo_bind(struct fbo *fbo)
{
#ifdef VULKAN_BACKEND
#else
    struct glcontext *gl = fbo->gl;

    ngli_glGetIntegerv(gl, GL_FRAMEBUFFER_BINDING, (GLint *)&fbo->prev_id);
    ngli_glBindFramebuffer(gl, GL_FRAMEBUFFER, fbo->id);
#endif

    return 0;
}

int ngli_fbo_unbind(struct fbo *fbo)
{
#ifdef VULKAN_BACKEND
#else
    struct glcontext *gl = fbo->gl;

    ngli_glBindFramebuffer(gl, GL_FRAMEBUFFER, fbo->prev_id);
    fbo->prev_id = 0;
#endif

    return 0;
}

void ngli_fbo_invalidate_depth_buffers(struct fbo *fbo)
{
#ifdef VULKAN_BACKEND
#else
    struct glcontext *gl = fbo->gl;

    if (!(gl->features & NGLI_FEATURE_INVALIDATE_SUBDATA))
        return;

    int nb_attachments = ngli_darray_count(&fbo->depth_indices);
    if (nb_attachments) {
        GLenum *attachments = ngli_darray_data(&fbo->depth_indices);
        ngli_glInvalidateFramebuffer(gl, GL_FRAMEBUFFER, nb_attachments, attachments);
    }
#endif
}

void ngli_fbo_blit(struct fbo *fbo, struct fbo *dst, int vflip)
{
#ifdef VULKAN_BACKEND
#else
    struct glcontext *gl = fbo->gl;

    if (!(gl->features & NGLI_FEATURE_FRAMEBUFFER_OBJECT))
        return;

    ngli_glBindFramebuffer(gl, GL_DRAW_FRAMEBUFFER, dst->id);
    if (vflip)
        ngli_glBlitFramebuffer(gl,
                               0, 0, fbo->width, fbo->height, 0, dst->height, dst->width, 0,
                               GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT,
                               GL_NEAREST);
    else
        ngli_glBlitFramebuffer(gl,
                               0, 0, fbo->width, fbo->height, 0, 0, dst->width, dst->height,
                               GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT,
                               GL_NEAREST);
    ngli_glBindFramebuffer(gl, GL_DRAW_FRAMEBUFFER, fbo->id);
#endif
}

void ngli_fbo_read_pixels(struct fbo *fbo, uint8_t *data)
{
#ifdef VULKAN_BACKEND
#else
    struct glcontext *gl = fbo->gl;
    ngli_glReadPixels(gl, 0, 0, fbo->width, fbo->height, GL_RGBA, GL_UNSIGNED_BYTE, data);
#endif
}

void ngli_fbo_reset(struct fbo *fbo)
{
#ifdef VULKAN_BACKEND
#else
    struct glcontext *gl = fbo->gl;
    if (!gl)
        return;

    ngli_glDeleteFramebuffers(gl, 1, &fbo->id);

    ngli_darray_reset(&fbo->depth_indices);
#endif

    memset(fbo, 0, sizeof(*fbo));
}
