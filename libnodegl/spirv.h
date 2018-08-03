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

#ifndef SPIRV_H
#define SPIRV_H

#include <stdlib.h>
#include <stdint.h>

enum {
    NGLI_SHADER_INPUT           = 1 << 0,
    NGLI_SHADER_OUTPUT          = 1 << 1,
    NGLI_SHADER_ATTRIBUTE       = 1 << 2,
    NGLI_SHADER_BLOCK           = 1 << 3,
    NGLI_SHADER_CONSTANT        = 1 << 4,
    NGLI_SHADER_SAMPLER         = 1 << 5,
    NGLI_SHADER_TEXTURE         = 1 << 6,
    NGLI_SHADER_UNIFORM         = 1 << 7,
    NGLI_SHADER_STORAGE         = 1 << 8,
    NGLI_SHADER_DYNAMIC         = 1 << 9,
    NGLI_SHADER_INDIRECTION     = 1 << 10
};

struct spirv_attribute {
    uint8_t index;
    uint16_t flag;
};

struct spirv_binding {
    uint16_t flag;
    uint8_t index;
};

struct spirv_variable {
    uint16_t offset;
    uint16_t flag;
};

struct spirv_block {
    struct spirv_binding binding;
    uint16_t size;
    struct hmap *variables;
};

struct spirv_texture {
    struct spirv_binding binding;
    uint32_t format;
};

struct spirv_probe {
    struct hmap *attributes;
    struct hmap *bindings;
};

struct spirv_probe *ngli_spirv_probe(const uint32_t *code, size_t size);
void ngli_spirv_freep(struct spirv_probe **probep);

#endif
