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

#include "buffer.h"
#include "nodes.h"

int ngli_buffer_init(struct buffer *s, struct ngl_ctx *ctx, int size, int usage)
{
    s->ctx = ctx;
    s->size = size;
    s->usage = usage;
    struct glcontext *vk = ctx->glcontext;

    /* Create buffer object */
    VkBufferCreateInfo buffer_create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    if (vkCreateBuffer(vk->device, &buffer_create_info, NULL, &s->vkbuf) != VK_SUCCESS)
        return -1;

    /* Allocate GPU memory and associate it with buffer object */
    VkMemoryRequirements mem_req;
    vkGetBufferMemoryRequirements(vk->device, s->vkbuf, &mem_req);
    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mem_req.size,
        .memoryTypeIndex = ngli_vk_find_memory_type(vk, mem_req.memoryTypeBits,
                                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
    };
    VkResult vkret = vkAllocateMemory(vk->device, &alloc_info, NULL, &s->vkmem);
    if (vkret != VK_SUCCESS)
        return -1;
    vkBindBufferMemory(vk->device, s->vkbuf, s->vkmem, 0);

    return 0;
}

int ngli_buffer_upload(struct buffer *s, void *data, int size)
{
    struct ngl_ctx *ctx = s->ctx;
    struct glcontext *vk = ctx->glcontext;
    void *mapped_mem;
    VkResult ret = vkMapMemory(vk->device, s->vkmem, 0, size, 0, &mapped_mem);
    if (ret != VK_SUCCESS)
        return -1;
    memcpy(mapped_mem, data, size);
    vkUnmapMemory(vk->device, s->vkmem);
    return 0;
}

void *ngli_buffer_map(struct buffer *s)
{
    struct ngl_ctx *ctx = s->ctx;
    struct glcontext *vk = ctx->glcontext;
    void *mapped_memory = NULL;
    vkMapMemory(vk->device, s->vkmem, 0, s->size, 0, &mapped_memory);
    return mapped_memory;
}

void ngli_buffer_unmap(struct buffer *s)
{
    struct ngl_ctx *ctx = s->ctx;
    struct glcontext *vk = ctx->glcontext;
    vkUnmapMemory(vk->device, s->vkmem);
}

void ngli_buffer_reset(struct buffer *s)
{
    struct ngl_ctx *ctx = s->ctx;
    if (!ctx)
        return;
    struct glcontext *vk = ctx->glcontext;
    vkDestroyBuffer(vk->device, s->vkbuf, NULL);
    vkFreeMemory(vk->device, s->vkmem, NULL);
    memset(s, 0, sizeof(*s));
}
