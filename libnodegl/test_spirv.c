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

#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hmap.h"
#include "spirv.h"
#include "memory.h"

static uint8_t *read_file(const char *filepath, uint32_t *size)
{
    uint8_t *buf = NULL;
    struct stat st;

    int fd = open(filepath, O_RDONLY);
    if (fd == -1) {
        fprintf(stderr, "unable to open \"%s\"\n", filepath);
        goto end;
    }

    if (fstat(fd, &st) == -1)
        goto end;

    buf = ngli_malloc(st.st_size + 1);
    if (!buf)
        goto end;

    int n = read(fd, buf, st.st_size);
    buf[n] = 0;
    *size = st.st_size;

end:
    if (fd != -1)
        close(fd);
    return buf;
}

static const char *storage_class_str[] = {
    [NGLI_SPIRV_STORAGE_CLASS_UNSUPPORTED]      = "Unsupported",
    [NGLI_SPIRV_STORAGE_CLASS_INPUT]            = "Input",
    [NGLI_SPIRV_STORAGE_CLASS_OUTPUT]           = "Output",
    [NGLI_SPIRV_STORAGE_CLASS_UNIFORM]          = "Uniform",
    [NGLI_SPIRV_STORAGE_CLASS_UNIFORM_CONSTANT] = "UniformConstant",
    [NGLI_SPIRV_STORAGE_CLASS_PUSH_CONSTANT]    = "PushConstant",
    [NGLI_SPIRV_STORAGE_CLASS_STORAGE_BUFFER]   = "StorageBuffer",
};

static const char *object_type_str[] = {
    [NGLI_SPIRV_OBJECT_TYPE_UNSUPPORTED]   = "Unsupported",
    [NGLI_SPIRV_OBJECT_TYPE_VARIABLE]      = "Variable",
    [NGLI_SPIRV_OBJECT_TYPE_FLOAT]         = "Float",
    [NGLI_SPIRV_OBJECT_TYPE_VEC2]          = "Vec2",
    [NGLI_SPIRV_OBJECT_TYPE_VEC3]          = "Vec3",
    [NGLI_SPIRV_OBJECT_TYPE_VEC4]          = "Vec4",
    [NGLI_SPIRV_OBJECT_TYPE_MAT4]          = "Mat4",
    [NGLI_SPIRV_OBJECT_TYPE_POINTER]       = "Pointer",
    [NGLI_SPIRV_OBJECT_TYPE_STRUCT]        = "Struct",
    [NGLI_SPIRV_OBJECT_TYPE_IMAGE]         = "Image",
    [NGLI_SPIRV_OBJECT_TYPE_SAMPLED_IMAGE] = "Sampler",
};

static void dump_block_defs(struct hmap *blocks)
{
    printf("Blocks:\n");
    const struct hmap_entry *entry = NULL;
    while ((entry = ngli_hmap_next(blocks, entry))) {
        const struct spirv_block *block = entry->data;
        printf("  %s (size=%d):\n", entry->key, block->size);

        const struct darray *block_members_array = &block->members;
        const struct spirv_block_member *block_members = ngli_darray_data(block_members_array);
        for (int i = 0; i < ngli_darray_count(block_members_array); i++) {
            const struct spirv_block_member *block_member = &block_members[i];
            printf("    %-12s %-32s @ %d\n",
                   object_type_str[block_member->type],
                   block_member->name, block_member->offset);
        }
    }
}

static void dump_variables(struct hmap *variables)
{
    printf("Variables:\n");
    const struct hmap_entry *entry = NULL;
    while ((entry = ngli_hmap_next(variables, entry))) {
        const struct spirv_variable *variable = entry->data;
        printf("  %-12s %-30s", object_type_str[variable->type], entry->key);
        printf("   dset:%3d", variable->descriptor_set);
        printf("   binding:%3d", variable->binding);
        printf("   location:%3d", variable->location);
        printf("   storage_class:%-15s", storage_class_str[variable->storage_class]);
        printf("\n");
    }
}

int main(int ac, char **av)
{
    if (ac != 2) {
        fprintf(stderr, "Usage: %s <file.spv>\n", av[0]);
        return -1;
    }

    uint32_t shader_size;
    uint8_t *shader_code = read_file(av[1], &shader_size);
    if (!shader_code)
        return -1;

    struct spirv_probe *s = ngli_spirv_probe((uint32_t *)shader_code, shader_size);
    ngli_free(shader_code);
    if (!s)
        return -1;

    dump_block_defs(s->blocks);
    printf("\n");
    dump_variables(s->variables);

    ngli_spirv_freep(&s);

    return 0;
}
