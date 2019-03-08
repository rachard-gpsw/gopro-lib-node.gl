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

#include "log.h"
#include "utils.h"
#include "format.h"
#include "glincludes.h"
#include "glcontext.h"
#include "texture.h"

#ifdef VULKAN_BACKEND
int ngli_texture_filter_has_mipmap(VkFilter filter)
{
    /* FIXME */
    return 0;
}

int ngli_texture_filter_has_linear_filtering(VkFilter filter)
{
    switch (filter) {
    case VK_FILTER_LINEAR:
        return 1;
    default:
        return 0;
    }
}
#else
int ngli_texture_filter_has_mipmap(GLint filter)
{
    switch (filter) {
    case GL_NEAREST_MIPMAP_NEAREST:
    case GL_NEAREST_MIPMAP_LINEAR:
    case GL_LINEAR_MIPMAP_NEAREST:
    case GL_LINEAR_MIPMAP_LINEAR:
        return 1;
    default:
        return 0;
    }
}

int ngli_texture_filter_has_linear_filtering(GLint filter)
{
    switch (filter) {
    case GL_LINEAR:
    case GL_LINEAR_MIPMAP_NEAREST:
    case GL_LINEAR_MIPMAP_LINEAR:
        return 1;
    default:
        return 0;
    }
}
#endif

#ifndef VULKAN_BACKEND
static void texture_set_image(struct texture *s, const uint8_t *data)
{
    struct glcontext *gl = s->gl;
    const struct texture_params *params = &s->params;

    switch (s->target) {
    case GL_TEXTURE_2D:
        ngli_glTexImage2D(gl, GL_TEXTURE_2D, 0, s->internal_format, params->width, params->height, 0, s->format, s->format_type, data);
        break;
    case GL_TEXTURE_3D:
        ngli_glTexImage3D(gl, GL_TEXTURE_3D, 0, s->internal_format, params->width, params->height, params->depth, 0, s->format, s->format_type, data);
        break;
    }
}

static void texture_set_sub_image(struct texture *s, const uint8_t *data)
{
    struct glcontext *gl = s->gl;
    const struct texture_params *params = &s->params;

    switch (s->target) {
    case GL_TEXTURE_2D:
        ngli_glTexSubImage2D(gl, GL_TEXTURE_2D, 0, 0, 0, params->width, params->height, s->format, s->format_type, data);
        break;
    case GL_TEXTURE_3D:
        ngli_glTexSubImage3D(gl, GL_TEXTURE_3D, 0, 0, 0, 0, params->width, params->height, params->depth, s->format, s->format_type, data);
        break;
    }
}

static void texture_set_storage(struct texture *s)
{
    struct glcontext *gl = s->gl;
    const struct texture_params *params = &s->params;

    switch (s->target) {
    case GL_TEXTURE_2D: {
        int mipmap_levels = 1;
        if (ngli_texture_has_mipmap(s))
            while ((params->width | params->height) >> mipmap_levels)
                mipmap_levels += 1;
        ngli_glTexStorage2D(gl, s->target, mipmap_levels, s->internal_format, params->width, params->height);
        break;
    }
    case GL_TEXTURE_3D:
        ngli_glTexStorage3D(gl, s->target, 1, s->internal_format, params->width, params->height, params->depth);
        break;
    }
}

static int renderbuffer_check_samples(struct texture *s)
{
    struct glcontext *gl = s->gl;
    const struct texture_params *params = &s->params;

    int max_samples = gl->max_samples;
    if (gl->features & NGLI_FEATURE_INTERNALFORMAT_QUERY)
        ngli_glGetInternalformativ(gl, GL_RENDERBUFFER, s->format, GL_SAMPLES, 1, &max_samples);

    if (params->samples > max_samples) {
        LOG(WARNING, "renderbuffer format 0x%x does not support samples %d (maximum %d)",
            s->format, params->samples, max_samples);
        return -1;
    }

    return 0;
}

static void renderbuffer_set_storage(struct texture *s)
{
    struct glcontext *gl = s->gl;
    const struct texture_params *params = &s->params;

    if (params->samples > 0)
        ngli_glRenderbufferStorageMultisample(gl, GL_RENDERBUFFER, params->samples, s->format, params->width, params->height);
    else
        ngli_glRenderbufferStorage(gl, GL_RENDERBUFFER, s->format, params->width, params->height);
}

static int texture_init_fields(struct texture *s)
{
    struct glcontext *gl = s->gl;
    const struct texture_params *params = &s->params;

    if (params->usage & NGLI_TEXTURE_USAGE_ATTACHMENT_ONLY) {
        s->target = GL_RENDERBUFFER;
        int ret = ngli_format_get_gl_renderbuffer_format(gl, params->format, &s->format);
        if (ret < 0)
            return ret;
        s->internal_format = s->format;

        ret = renderbuffer_check_samples(s);
        if (ret < 0)
            return ret;
        return 0;
    }

    /* TODO: add multisample support for textures */
    ngli_assert(!params->samples);

    if (params->dimensions == 2)
        s->target = GL_TEXTURE_2D;
    else if (params->dimensions == 3)
        s->target = GL_TEXTURE_3D;
    else
        ngli_assert(0);

    if (params->external_oes) {
        ngli_assert(params->dimensions == 2);
        s->target = GL_TEXTURE_EXTERNAL_OES;
    } else if (params->rectangle) {
        ngli_assert(params->dimensions == 2);
        s->target = GL_TEXTURE_RECTANGLE;
    }

    int ret = ngli_format_get_gl_texture_format(gl,
                                                params->format,
                                                &s->format,
                                                &s->internal_format,
                                                &s->format_type);
    if (ret < 0)
        return ret;

    if (params->external_storage || params->external_oes)
        s->external_storage = 1;

    return 0;
}
#else
static int find_memory_type(struct glcontext *vk, uint32_t type_filter, VkMemoryPropertyFlags props)
{
    for (int i = 0; i < vk->phydev_mem_props.memoryTypeCount; i++)
        if ((type_filter & (1<<i)) && (vk->phydev_mem_props.memoryTypes[i].propertyFlags & props) == props)
            return i;
    return -1;
}

static VkResult create_buffer(struct glcontext *vk, VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer *buffer, VkDeviceMemory *buffer_memory)
{
    VkBufferCreateInfo buffer_create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VkResult ret = vkCreateBuffer(vk->device, &buffer_create_info, NULL, buffer);
    if (ret != VK_SUCCESS)
        return ret;

    VkMemoryRequirements mem_requirements;
    vkGetBufferMemoryRequirements(vk->device, *buffer, &mem_requirements);

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mem_requirements.size,
        .memoryTypeIndex = find_memory_type(vk, mem_requirements.memoryTypeBits, properties),
    };

    ret = vkAllocateMemory(vk->device, &alloc_info, NULL, buffer_memory);
    if (ret != VK_SUCCESS)
        return ret;

    vkBindBufferMemory(vk->device, *buffer, *buffer_memory, 0);

    return VK_SUCCESS;
}

static VkResult create_image(struct glcontext *vk, uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage *image, VkDeviceMemory *imageMemory)
{
    VkImageCreateInfo image_create_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .extent.width = width,
        .extent.height = height,
        .extent.depth = 1,
        .mipLevels = 1,
        .arrayLayers = 1,
        .format = format,
        .tiling = tiling,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .usage = usage,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VkResult ret = vkCreateImage(vk->device, &image_create_info, NULL, image);
    if (ret != VK_SUCCESS)
        return ret;

    VkMemoryRequirements mem_requirements;
    vkGetImageMemoryRequirements(vk->device, *image, &mem_requirements);

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mem_requirements.size,
        .memoryTypeIndex = find_memory_type(vk, mem_requirements.memoryTypeBits, properties),
    };

    ret = vkAllocateMemory(vk->device, &alloc_info, NULL, imageMemory);
    if (ret != VK_SUCCESS)
        return ret;

    vkBindImageMemory(vk->device, *image, *imageMemory, 0);

    return VK_SUCCESS;
}


#endif

#ifdef VULKAN_BACKEND
static VkResult transition_image_layout(struct texture *s, VkImage image, VkFormat format, VkImageLayout old_layout, VkImageLayout new_layout);
#endif

int ngli_texture_init(struct texture *s,
                      struct glcontext *gl,
                      const struct texture_params *params)
{
    s->gl = gl;
    s->params = *params;

#ifdef VULKAN_BACKEND
    struct glcontext *vk = gl;
    s->image_size = s->params.width * s->params.height * 4;

    VkCommandPoolCreateInfo command_pool_create_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = vk->queue_family_graphics_id,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
    };

    /* FIXME: check return */
    vkCreateCommandPool(vk->device, &command_pool_create_info, NULL, &s->command_pool);

    create_buffer(vk, s->image_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &s->buffer, &s->buffer_memory);

    /* FIXME */
    ngli_format_get_vk_format(vk, s->params.format, &s->format);

    VkImageUsageFlagBits usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    create_image(vk, s->params.width, s->params.height, s->format, VK_IMAGE_TILING_OPTIMAL, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &s->image, &s->image_memory);
    s->image_layout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImageViewCreateInfo view_info = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = s->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = s->format,
        .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .subresourceRange.baseMipLevel = 0,
        .subresourceRange.levelCount = 1,
        .subresourceRange.baseArrayLayer = 0,
        .subresourceRange.layerCount = 1,
    };

    LOG(ERROR, ">>");
    if (vkCreateImageView(vk->device, &view_info, NULL, &s->image_view) != VK_SUCCESS) {
        return -1;
    }
    LOG(ERROR, "image_view=%p", s->image_view);

    transition_image_layout(s, s->image, s->format, s->image_layout, VK_IMAGE_LAYOUT_GENERAL);

    VkSamplerCreateInfo sampler_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = s->params.mag_filter,
        .minFilter = s->params.min_filter,
        .addressModeU = s->params.wrap_s,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .anisotropyEnable = VK_FALSE,
        .maxAnisotropy = 0,
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_ALWAYS,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
    };

    if (vkCreateSampler(vk->device, &sampler_info, NULL, &s->image_sampler) != VK_SUCCESS) {
        return -1;
    }
#else
    int ret = texture_init_fields(s);
    if (ret < 0)
        return ret;

    if (s->target == GL_RENDERBUFFER) {
        ngli_glGenRenderbuffers(gl, 1, &s->id);
        ngli_glBindRenderbuffer(gl, s->target, s->id);
        renderbuffer_set_storage(s);
    } else {
        ngli_glGenTextures(gl, 1, &s->id);
        ngli_glBindTexture(gl, s->target, s->id);
        ngli_glTexParameteri(gl, s->target, GL_TEXTURE_MIN_FILTER, params->min_filter);
        ngli_glTexParameteri(gl, s->target, GL_TEXTURE_MAG_FILTER, params->mag_filter);
        ngli_glTexParameteri(gl, s->target, GL_TEXTURE_WRAP_S, params->wrap_s);
        ngli_glTexParameteri(gl, s->target, GL_TEXTURE_WRAP_T, params->wrap_t);
        if (s->target == GL_TEXTURE_3D)
            ngli_glTexParameteri(gl, s->target, GL_TEXTURE_WRAP_R, params->wrap_r);

        if (!s->external_storage) {
            if (!params->width || !params->height ||
                (params->dimensions == 3 && !params->depth)) {
                LOG(ERROR, "invalid texture dimensions %dx%dx%d",
                    params->width, params->height, params->depth);
                ngli_texture_reset(s);
                return -1;
            }
            if (params->immutable) {
                texture_set_storage(s);
            } else {
                texture_set_image(s, NULL);
            }
        }
    }
#endif

    return 0;
}

#ifndef VULKAN_BACKEND
int ngli_texture_wrap(struct texture *s,
                      struct glcontext *gl,
                      const struct texture_params *params,
                      GLuint texture)
{
    s->gl = gl;
    s->params = *params;

    int ret = texture_init_fields(s);
    if (ret < 0)
        return ret;

    s->id = texture;
    s->wrapped = 1;
    s->external_storage = 1;

    return 0;
}

void ngli_texture_set_id(struct texture *s, GLuint id)
{
    /* only wrapped textures can update their id with this function */
    ngli_assert(s->wrapped);

    s->id = id;
}

void ngli_texture_set_dimensions(struct texture *s, int width, int height, int depth)
{
    /* only textures with external storage can update their dimensions with this function */
    ngli_assert(s->external_storage);

    struct texture_params *params = &s->params;
    params->width = width;
    params->height = height;
    params->depth = depth;
}
#endif

int ngli_texture_has_mipmap(const struct texture *s)
{
    return ngli_texture_filter_has_mipmap(s->params.min_filter);
}

int ngli_texture_has_linear_filtering(const struct texture *s)
{
    return ngli_texture_filter_has_linear_filtering(s->params.min_filter);
}

int ngli_texture_match_dimensions(const struct texture *s, int width, int height, int depth)
{
    const struct texture_params *params = &s->params;
    return params->width == width && params->height == height && params->depth == depth;
}

#ifdef VULKAN_BACKEND
static VkCommandBuffer begin_single_time_command(struct texture *s)
{
    struct glcontext *vk = s->gl;

    VkCommandBufferAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandPool = s->command_pool,
        .commandBufferCount = 1,
    };

    VkCommandBuffer command_buffer;
    vkAllocateCommandBuffers(vk->device, &alloc_info, &command_buffer);

    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };

    vkBeginCommandBuffer(command_buffer, &beginInfo);

    return command_buffer;
}

static VkResult end_single_command(struct texture *s, VkCommandBuffer command_buffer)
{
    struct glcontext *vk = s->gl;

    vkEndCommandBuffer(command_buffer);

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &command_buffer,
    };

    VkQueue graphic_queue;
    vkGetDeviceQueue(vk->device, vk->queue_family_graphics_id, 0, &graphic_queue);

    VkResult ret = vkQueueSubmit(graphic_queue, 1, &submit_info, VK_NULL_HANDLE);
    if (ret != VK_SUCCESS)
        return ret;

    vkQueueWaitIdle(graphic_queue);
    vkFreeCommandBuffers(vk->device, s->command_pool, 1, &command_buffer);

    return ret;
}

static VkResult copy_buffer_to_image(struct texture *s, VkBuffer buffer, VkImage image, uint32_t width, uint32_t height)
{
    VkCommandBuffer command_buffer = begin_single_time_command(s);
    if (!command_buffer)
        return -1;

    VkBufferImageCopy region = {
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .imageSubresource.mipLevel = 0,
        .imageSubresource.baseArrayLayer = 0,
        .imageSubresource.layerCount = 1,
        .imageOffset = {0, 0, 0},
        .imageExtent = {
            width,
            height,
            1,
        }
    };

    vkCmdCopyBufferToImage(command_buffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    return end_single_command(s, command_buffer);
}

static VkResult transition_image_layout(struct texture *s, VkImage image, VkFormat format, VkImageLayout old_layout, VkImageLayout new_layout)
{
    VkCommandBuffer command_buffer = begin_single_time_command(s);
    if (!command_buffer)
        return -1;

    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .subresourceRange.baseMipLevel = 0,
        .subresourceRange.levelCount = 1,
        .subresourceRange.baseArrayLayer = 0,
        .subresourceRange.layerCount = 1,
    };

    VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;

    switch (old_layout) {
    case VK_IMAGE_LAYOUT_UNDEFINED:
        barrier.srcAccessMask = 0;
        break;
    case VK_IMAGE_LAYOUT_PREINITIALIZED:
        barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        break;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        break;
    case VK_IMAGE_LAYOUT_GENERAL:
        /* FIXME */
        break;
    default:
        ngli_assert(0);
    }

    switch (new_layout) {
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        break;
    case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
        barrier.dstAccessMask = barrier.dstAccessMask | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        if (barrier.srcAccessMask == 0)
            barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        break;
    case VK_IMAGE_LAYOUT_GENERAL:
        /* FIXME */
        break;
    default:
            ngli_assert(0);
    }

    vkCmdPipelineBarrier(command_buffer, src_stage, dst_stage, 0, 0, NULL, 0, NULL, 1, &barrier);

    /* FIXME: check end_single_command first */
    s->image_layout = new_layout;
    return end_single_command(s, command_buffer);
}
#endif

int ngli_texture_upload(struct texture *s, const uint8_t *data)
{
    struct glcontext *gl = s->gl;
    const struct texture_params *params = &s->params;

    /* texture with external storage (including wrapped textures and render
     * buffers) cannot update their content with this function */
    ngli_assert(!s->external_storage && !(params->usage & NGLI_TEXTURE_USAGE_ATTACHMENT_ONLY));

#ifdef VULKAN_BACKEND
    struct glcontext *vk = gl;
    if (data) {
        void *mapped_data;
        vkMapMemory(vk->device, s->buffer_memory, 0, s->image_size, 0, &mapped_data);
        memcpy(mapped_data, data, s->image_size);
        vkUnmapMemory(vk->device, s->buffer_memory);

        transition_image_layout(s, s->image, s->format, s->image_layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copy_buffer_to_image(s, s->buffer, s->image, params->width, params->height);
        transition_image_layout(s, s->image, s->format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    }
#else
    ngli_glBindTexture(gl, s->target, s->id);
    if (data) {
        texture_set_sub_image(s, data);
        if (ngli_texture_has_mipmap(s))
            ngli_glGenerateMipmap(gl, s->target);
    }
    ngli_glBindTexture(gl, s->target, 0);
#endif

    return 0;
}

int ngli_texture_generate_mipmap(struct texture *s)
{
    struct glcontext *gl = s->gl;
    const struct texture_params *params = &s->params;

    ngli_assert(!(params->usage & NGLI_TEXTURE_USAGE_ATTACHMENT_ONLY));

#ifdef VULKAN_BACKEND
#else
    ngli_glBindTexture(gl, s->target, s->id);
    ngli_glGenerateMipmap(gl, s->target);
#endif
    return 0;
}

void ngli_texture_reset(struct texture *s)
{
    struct glcontext *gl = s->gl;
    if (!gl)
        return;

#ifdef VULKAN_BACKEND
    struct glcontext *vk = s->gl;

    vkDestroySampler(vk->device, s->image_sampler, NULL);
    vkDestroyImageView(vk->device, s->image_view, NULL);
    vkDestroyImage(vk->device, s->image, NULL);
    vkDestroyBuffer(vk->device, s->buffer, NULL);
    vkFreeMemory(vk->device, s->buffer_memory, NULL);
    vkFreeMemory(vk->device, s->image_memory, NULL);
    vkDestroyCommandPool(vk->device, s->command_pool, NULL);
#else
    if (!s->wrapped) {
        if (s->target == GL_RENDERBUFFER)
            ngli_glDeleteRenderbuffers(gl, 1, &s->id);
        else
            ngli_glDeleteTextures(gl, 1, &s->id);
    }
#endif

    memset(s, 0, sizeof(*s));
}
