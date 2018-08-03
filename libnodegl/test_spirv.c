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

#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hmap.h"
#include "spirv.h"
#include "memory.h"

static char *read_file(const char *filepath, uint32_t *size)
{
    char *buf = NULL;
    struct stat st;

    int fd = open(filepath, O_RDONLY);
    if (fd == -1)
        goto end;

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

#include <string.h>

static const char *get_flag_name(uint32_t flag) {
    static char result[64];
    static const char *names[]= {
        "input",
        "output",
        "attribute",
        "block",
        "constant",
        "sampler",
        "texture",
        "uniforn",
        "storage",
        "dynamic",
        "indirection"
    };

    memset(result, 0, sizeof(result));
    char *cursor = result;
    static uint32_t nb_flags = sizeof(names) / sizeof(names[0]);
    for (uint8_t i = 0; i < nb_flags; i++) {
        if ((flag & (1 << i)) != 0) {
            uint32_t length = strlen(names[i]);
            memcpy(cursor, names[i], length);
            cursor[length] = ' ';
            cursor = cursor + length + 1;
        }
    }
    *cursor = 0;

    return result;
}

int main(int argc, char *argv[])
{
    const char *filepath = argv[1];

    uint32_t shader_size;
    char *shader_code = read_file(filepath, &shader_size);
    if (!shader_code)
        return -1;

    struct spirv_probe *s = ngli_spirv_probe((uint32_t*)shader_code, shader_size);
    if (!s)
        return -1;
    ngli_free(shader_code);

    printf("filename: %s\n\n", filepath);

    if (s->attributes) {
        printf("attributes: (%d)\n", ngli_hmap_count(s->attributes));
        const struct hmap_entry *attribute_entry = NULL;
        while ((attribute_entry = ngli_hmap_next(s->attributes, attribute_entry))) {
            const struct spirv_attribute *attribute = attribute_entry->data;
            printf("\t%s:\n", attribute_entry->key);
            printf("\t\ttype: %s\n", get_flag_name(attribute->flag));
            printf("\t\tindex: %d\n", attribute->index);
         }
    }

    printf("\nbindings: (%d)\n", s->bindings ? ngli_hmap_count(s->bindings) : 0);
    if (s->bindings) {
        const struct hmap_entry *binding_entry = NULL;
        while ((binding_entry = ngli_hmap_next(s->bindings, binding_entry))) {
            const struct spirv_binding *binding = binding_entry->data;
            if ((binding->flag & NGLI_SHADER_BLOCK)) {
                const struct spirv_block *block = binding_entry->data;
                printf("\t%s:\n", binding_entry->key);
                printf("\t\ttype: %s\n", get_flag_name(binding->flag));
                printf("\t\tindex: %d\n", binding->index);
                printf("\t\tsize: %d\n", block->size);
                printf("\t\tvariables:\n");
                if (block->variables) {
                    const struct hmap_entry *variable_entry = NULL;
                    while ((variable_entry = ngli_hmap_next(block->variables, variable_entry))) {
                        const struct spirv_variable *variable = variable_entry->data;
                        printf("\t\t\t%20s offset: %d\n", variable_entry->key, variable->offset);
                    }
                }
            } else if ((binding->flag & NGLI_SHADER_TEXTURE)) {
                const struct spirv_texture *texture = binding_entry->data;
                printf("\t\t%s\ttype: %s\tindex: %d\tformat: %d\n",
                       binding_entry->key,
                       get_flag_name(binding->flag),
                       binding->index,
                       texture->format);
            }
        }
    }
    ngli_spirv_freep(&s);

    return 0;
}
