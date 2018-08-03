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

#include "log.h"
#include "utils.h"
#include "format.h"
#include "glincludes.h"
#include "glcontext.h"
#include "texture.h"

static const GLint vk_filter_map[NGLI_NB_FILTER] = {
    [NGLI_FILTER_NEAREST] = VK_FILTER_NEAREST,
    [NGLI_FILTER_LINEAR]  = VK_FILTER_LINEAR,
};

VkFilter ngli_texture_get_vk_filter(int filter)
{
    return vk_filter_map[filter];
}

static const GLint vk_mipmap_mode_map[NGLI_NB_FILTER] = {
    [NGLI_FILTER_NEAREST] = VK_SAMPLER_MIPMAP_MODE_NEAREST,
    [NGLI_FILTER_LINEAR]  = VK_SAMPLER_MIPMAP_MODE_LINEAR,
};

VkFilter ngli_texture_get_vk_mipmap_mode(int mipmap_filter)
{
    return vk_mipmap_mode_map[mipmap_filter];
}

static const VkSamplerAddressMode vk_wrap_map[NGLI_NB_WRAP] = {
    [NGLI_WRAP_CLAMP_TO_EDGE]   = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    [NGLI_WRAP_MIRRORED_REPEAT] = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
    [NGLI_WRAP_REPEAT]          = VK_SAMPLER_ADDRESS_MODE_REPEAT,
};

VkSamplerAddressMode ngli_texture_get_vk_wrap(int wrap)
{
    return vk_wrap_map[wrap];
}

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

static VkResult transition_image_layout(struct texture *s, VkImage image, VkFormat format, VkImageLayout old_layout, VkImageLayout new_layout);

int ngli_texture_init(struct texture *s,
                      struct glcontext *gl,
                      const struct texture_params *params)
{
    s->gl = gl;
    s->params = *params;

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

    VkImageUsageFlagBits usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
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

    if (vkCreateImageView(vk->device, &view_info, NULL, &s->image_view) != VK_SUCCESS) {
        return -1;
    }

    transition_image_layout(s, s->image, s->format, s->image_layout, VK_IMAGE_LAYOUT_GENERAL);

    VkSamplerCreateInfo sampler_info = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = ngli_texture_get_vk_filter(s->params.mag_filter),
        .minFilter = ngli_texture_get_vk_filter(s->params.min_filter),
        .addressModeU = ngli_texture_get_vk_wrap(s->params.wrap_s),
        .addressModeV = ngli_texture_get_vk_wrap(s->params.wrap_t),
        .addressModeW = ngli_texture_get_vk_wrap(s->params.wrap_r),
        .anisotropyEnable = VK_FALSE,
        .maxAnisotropy = 0,
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_ALWAYS,
        .mipmapMode = ngli_texture_get_vk_mipmap_mode(s->params.mipmap_filter),
    };

    if (vkCreateSampler(vk->device, &sampler_info, NULL, &s->image_sampler) != VK_SUCCESS) {
        return -1;
    }

    return 0;
}

int ngli_texture_has_mipmap(const struct texture *s)
{
    return s->params.mipmap_filter != NGLI_MIPMAP_FILTER_NONE;
}

int ngli_texture_match_dimensions(const struct texture *s, int width, int height, int depth)
{
    const struct texture_params *params = &s->params;
    return params->width == width && params->height == height && params->depth == depth;
}

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

int ngli_texture_upload(struct texture *s, const uint8_t *data, int linesize)
{
    struct glcontext *gl = s->gl;
    const struct texture_params *params = &s->params;

    /* texture with external storage (including wrapped textures and render
     * buffers) cannot update their content with this function */
    ngli_assert(!s->external_storage && !(params->usage & NGLI_TEXTURE_USAGE_ATTACHMENT_ONLY));

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

    return 0;
}

int ngli_texture_generate_mipmap(struct texture *s)
{
    const struct texture_params *params = &s->params;

    ngli_assert(!(params->usage & NGLI_TEXTURE_USAGE_ATTACHMENT_ONLY));

    /* TODO */

    return 0;
}

void ngli_texture_reset(struct texture *s)
{
    struct glcontext *gl = s->gl;
    if (!gl)
        return;

    struct glcontext *vk = s->gl;

    vkDestroySampler(vk->device, s->image_sampler, NULL);
    vkDestroyImageView(vk->device, s->image_view, NULL);
    vkDestroyImage(vk->device, s->image, NULL);
    vkDestroyBuffer(vk->device, s->buffer, NULL);
    vkFreeMemory(vk->device, s->buffer_memory, NULL);
    vkFreeMemory(vk->device, s->image_memory, NULL);
    vkDestroyCommandPool(vk->device, s->command_pool, NULL);

    memset(s, 0, sizeof(*s));
}
