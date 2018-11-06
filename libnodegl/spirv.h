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

#include "hmap.h"
#include "darray.h"

enum storage_class {
    NGLI_SPIRV_STORAGE_CLASS_UNSUPPORTED,
    NGLI_SPIRV_STORAGE_CLASS_INPUT,
    NGLI_SPIRV_STORAGE_CLASS_OUTPUT,
    NGLI_SPIRV_STORAGE_CLASS_UNIFORM,
    NGLI_SPIRV_STORAGE_CLASS_UNIFORM_CONSTANT,
    NGLI_SPIRV_STORAGE_CLASS_PUSH_CONSTANT,
    NGLI_SPIRV_STORAGE_CLASS_STORAGE_BUFFER,
};

enum object_type {
    NGLI_SPIRV_OBJECT_TYPE_UNSUPPORTED,
    NGLI_SPIRV_OBJECT_TYPE_VARIABLE,
    NGLI_SPIRV_OBJECT_TYPE_FLOAT,
    NGLI_SPIRV_OBJECT_TYPE_VEC2,
    NGLI_SPIRV_OBJECT_TYPE_VEC3,
    NGLI_SPIRV_OBJECT_TYPE_VEC4,
    NGLI_SPIRV_OBJECT_TYPE_MAT4,
    NGLI_SPIRV_OBJECT_TYPE_POINTER,
    NGLI_SPIRV_OBJECT_TYPE_STRUCT,
    NGLI_SPIRV_OBJECT_TYPE_IMAGE,
    NGLI_SPIRV_OBJECT_TYPE_SAMPLED_IMAGE,
    NGLI_SPIRV_OBJECT_TYPE_BLOCK,
};

struct spirv_variable {
    int descriptor_set;
    int binding;
    int location;
    enum storage_class storage_class;
    enum object_type type;
};

struct spirv_block_member {
    char *name;
    enum storage_class storage_class;
    enum object_type type;
    int offset;
};

struct spirv_block {
    int size;                   // total size in bytes
    struct darray members;      // struct spirv_block_member
};

struct spirv_probe {
    struct hmap *blocks;        // struct spirv_block
    struct hmap *variables;     // struct spirv_variable
};

struct spirv_probe *ngli_spirv_probe(const uint32_t *code, size_t size);
void ngli_spirv_freep(struct spirv_probe **probep);

#endif
