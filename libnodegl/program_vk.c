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

#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "memory.h"
#include "nodes.h"
#include "program.h"
#include "spirv.h"

int ngli_program_init(struct program *s, struct ngl_ctx *ctx,
                      const uint8_t *vert_data, int vert_data_size,
                      const uint8_t *frag_data, int frag_data_size,
                      const uint8_t *comp_data, int comp_data_size)
{
    struct {
        const uint32_t *code;
        size_t code_size;
    } shaders[] = {
        [NGLI_PROGRAM_SHADER_VERT] = {(const uint32_t *)vert_data, vert_data_size},
        [NGLI_PROGRAM_SHADER_FRAG] = {(const uint32_t *)frag_data, frag_data_size},
        [NGLI_PROGRAM_SHADER_COMP] = {(const uint32_t *)comp_data, comp_data_size},
    };

    struct glcontext *vk = ctx->glcontext;

    s->ctx = ctx;

    for (int i = 0; i < NGLI_ARRAY_NB(s->shaders); i++) {
        if (!shaders[i].code)
            continue;
        struct program_shader *shader = &s->shaders[i];
        VkShaderModuleCreateInfo shader_module_create_info = {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = shaders[i].code_size,
            .pCode = shaders[i].code,
        };
        VkResult ret = vkCreateShaderModule(vk->device, &shader_module_create_info, NULL, &shader->vkmodule);
        if (ret != VK_SUCCESS)
            return -1;
        shader->probe = ngli_spirv_probe(shaders[i].code, shaders[i].code_size);
        if (!shader->probe)
            return -1;
    }

    return 0;
}

void ngli_program_reset(struct program *s)
{
    if (!s->ctx)
        return;
    struct glcontext *vk = s->ctx->glcontext;
    for (int i = 0; i < NGLI_ARRAY_NB(s->shaders); i++) {
        struct program_shader *shader = &s->shaders[i];
        ngli_spirv_freep(&shader->probe);
        vkDestroyShaderModule(vk->device, shader->vkmodule, NULL);
    }
    memset(s, 0, sizeof(*s));
}
