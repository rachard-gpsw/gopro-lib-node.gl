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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "darray.h"
#include "hmap.h"
#include "log.h"
#include "spirv.h"
#include "utils.h"
#include "memory.h"

struct spirv_header {
    uint32_t magic;
    uint32_t version;
    uint32_t gen_magic;
    uint32_t bound;
    uint32_t reserved;
};

struct obj_member {
    uint32_t index;
    char *name;
    uint32_t offset;
    enum object_type type;
};

struct obj {
    uint32_t id;
    char *name;
    struct darray members;

    /* type */
    enum object_type type;
    uint32_t size;
    uint32_t target;

    /* variable? */
    uint32_t descriptor_set;
    uint32_t binding;
    uint32_t location;
    enum object_type target_type;

    enum storage_class storage_class;
};

static void free_member(struct obj_member *member)
{
    ngli_free(member->name);
}

static void free_obj_members(struct darray *obj_members_array)
{
    struct obj_member *obj_members = ngli_darray_data(obj_members_array);
    for (int i = 0; i < ngli_darray_count(obj_members_array); i++)
        free_member(&obj_members[i]);
    ngli_darray_reset(obj_members_array);
}

static void free_obj(struct obj *obj)
{
    free_obj_members(&obj->members);
    ngli_free(obj->name);
}

static void free_objs(struct darray *objs_array)
{
    struct obj *objs = ngli_darray_data(objs_array);
    for (int i = 0; i < ngli_darray_count(objs_array); i++)
        free_obj(&objs[i]);
    ngli_darray_reset(objs_array);
}

static struct obj *get_obj(struct darray *objs_array, uint32_t id)
{
    struct obj *objs = ngli_darray_data(objs_array);
    for (int i = 0; i < ngli_darray_count(objs_array); i++)
        if (objs[i].id == id)
            return &objs[i];

    struct obj *obj = ngli_darray_push(objs_array, NULL);
    if (!obj)
        return NULL;
    memset(obj, 0, sizeof(*obj));
    obj->id = id;
    ngli_darray_init(&obj->members, sizeof(struct obj_member), 0);
    return obj;
}

// FIXME: non-lazy alloc so params can be made const
static struct obj_member *get_obj_member(struct darray *objs_array, uint32_t id, uint32_t index)
{
    struct obj *obj = get_obj(objs_array, id);
    if (!obj)
        return NULL;

    struct darray *members_array = &obj->members;
    struct obj_member *members = ngli_darray_data(members_array);
    for (int i = 0; i < ngli_darray_count(members_array); i++)
        if (members[i].index == index)
            return &members[i];

    struct obj_member *member = ngli_darray_push(&obj->members, NULL);
    if (!member)
        return NULL;
    memset(member, 0, sizeof(*member));
    member->index = index;
    return member;
}

static char *get_spv_string(const uint32_t *code, size_t code_size, int code_pos)
{
    const char *str = (const char *)&code[code_pos];
    code_size -= code_pos * sizeof(*code);

    size_t len = 0;
    while (len < code_size && str[len])
        len++;

    /* original source string may not be nul-terminated (maliciously) so we do
     * it manually */
    char *dupstr = ngli_malloc(len + 1);
    if (!dupstr)
        return NULL;
    memcpy(dupstr, str, len);
    dupstr[len] = 0;

    return dupstr;
}

static int op_name(struct darray *obj_array, const uint32_t *code, size_t code_size)
{
    struct obj *obj = get_obj(obj_array, code[1]);
    if (!obj)
        return -1;
    ngli_free(obj->name);
    obj->name = get_spv_string(code, code_size, 2);
    if (!obj->name)
        return -1;
    return 0;
}

static int op_membername(struct darray *objs_array, const uint32_t *code, size_t code_size)
{
    struct obj_member *member = get_obj_member(objs_array, code[1], code[2]);
    if (!member)
        return -1;
    ngli_free(member->name);
    member->name = get_spv_string(code, code_size, 3);
    if (!member->name)
        return -1;
    return 0;
}

static int op_typefloat(struct darray *objs_array, const uint32_t *code, size_t code_size)
{
    struct obj *result = get_obj(objs_array, code[1]);
    result->size = code[2] / 8;
    if (result->size == 4)
        result->type = NGLI_SPIRV_OBJECT_TYPE_FLOAT;
    return 0;
}

static int op_typevector(struct darray *objs_array, const uint32_t *code, size_t code_size)
{
    struct obj *result           = get_obj(objs_array, code[1]);
    const struct obj *comp_type  = get_obj(objs_array, code[2]);
    const uint32_t comp_count    = code[3];
    result->size = comp_type->size * comp_count;
    switch (comp_count) {
        case 2: result->type = NGLI_SPIRV_OBJECT_TYPE_VEC2; break;
        case 3: result->type = NGLI_SPIRV_OBJECT_TYPE_VEC3; break;
        case 4: result->type = NGLI_SPIRV_OBJECT_TYPE_VEC4; break;
    }
    return 0;
}

static int op_typematrix(struct darray *objs_array, const uint32_t *code, size_t code_size)
{
    struct obj *result         = get_obj(objs_array, code[1]);
    const struct obj *col_type = get_obj(objs_array, code[2]);
    if (!result || !col_type)
        return -1;
    result->size = col_type->size * code[3];
    if (result->size == 64)
        result->type = NGLI_SPIRV_OBJECT_TYPE_MAT4;
    return 0;
}

static int op_typeimage(struct darray *objs_array, const uint32_t *code, size_t code_size)
{
    struct obj *result = get_obj(objs_array, code[1]);
    if (!result)
        return -1;
    result->type = NGLI_SPIRV_OBJECT_TYPE_IMAGE;
    return 0;
}

static int op_typesampledimage(struct darray *objs_array, const uint32_t *code, size_t code_size)
{
    struct obj *result = get_obj(objs_array, code[1]);
    if (!result)
        return -1;
    result->target = code[2];
    result->type = NGLI_SPIRV_OBJECT_TYPE_SAMPLED_IMAGE;

    struct obj *target = get_obj(objs_array, result->target);
    if (!target)
        return -1;
    return 0;
}

static int op_typestruct(struct darray *objs_array, const uint32_t *code, size_t code_size)
{
    struct obj *result = get_obj(objs_array, code[1]);
    if (!result)
        return -1;

    const int nb_members = code_size/sizeof(*code) - 2;
    if (nb_members != result->members.count)
        return -1;

    struct obj_member *members = ngli_darray_data(&result->members);
    for (uint32_t i = 0; i < nb_members; i++) {
        struct obj *member_obj = get_obj(objs_array, code[2 + i]);
        if (!member_obj)
            return -1;
        if (result->size + member_obj->size < result->size)
            return -1;
        result->size += member_obj->size;

        members[i].type = member_obj->type;
    }
    if (result->type != NGLI_SPIRV_OBJECT_TYPE_BLOCK)
        result->type = NGLI_SPIRV_OBJECT_TYPE_STRUCT;
    return 0;
}

static enum storage_class get_storage_class(uint32_t value)
{
    static const enum storage_class st_classes[] = {
        [ 0] = NGLI_SPIRV_STORAGE_CLASS_UNIFORM_CONSTANT,
        [ 1] = NGLI_SPIRV_STORAGE_CLASS_INPUT,
        [ 2] = NGLI_SPIRV_STORAGE_CLASS_UNIFORM,
        [ 3] = NGLI_SPIRV_STORAGE_CLASS_OUTPUT,
        [ 9] = NGLI_SPIRV_STORAGE_CLASS_PUSH_CONSTANT,
        [12] = NGLI_SPIRV_STORAGE_CLASS_STORAGE_BUFFER,
    };
    if (value >= NGLI_ARRAY_NB(st_classes))
        return NGLI_SPIRV_STORAGE_CLASS_UNSUPPORTED;
    return st_classes[value];
}

static int op_typepointer(struct darray *objs_array, const uint32_t *code, size_t code_size)
{
    struct obj *result = get_obj(objs_array, code[1]);
    if (!result)
        return -1;
    result->storage_class = get_storage_class(code[2]);
    result->target = code[3];
    result->type = NGLI_SPIRV_OBJECT_TYPE_POINTER;

    struct obj *target = get_obj(objs_array, result->target);
    if (!target)
        return -1;
    result->target_type = target->type;
    return 0;
}

static int op_variable(struct darray *objs_array, const uint32_t *code, size_t code_size)
{
    struct obj *pointer = get_obj(objs_array, code[1]);
    struct obj *result  = get_obj(objs_array, code[2]);

    result->storage_class = get_storage_class(code[3]);

    result->type = NGLI_SPIRV_OBJECT_TYPE_VARIABLE;
    if (pointer->type != NGLI_SPIRV_OBJECT_TYPE_POINTER ||
        result->storage_class != pointer->storage_class)
        return -1;

    result->target = pointer->target;
    result->target_type = pointer->target_type;

    return 0;
}

static int op_decorate(struct darray *objs_array, const uint32_t *code, size_t code_size)
{
    struct obj *obj = get_obj(objs_array, code[1]);
    if (!obj)
        return -1;

    const uint32_t decoration = code[2];
    const uint32_t extra = code_size > 3 * sizeof(*code) ? code[3] : 0;

    switch (decoration) {
        //case 2:  obj->type = NGLI_SPIRV_OBJECT_TYPE_STRUCT; break;

        case 30: obj->location       = extra; break;
        case 33: obj->binding        = extra; break;
        case 34: obj->descriptor_set = extra; break;
    }
    return 0;
}

static int op_memberdecorate(struct darray *objs_array, const uint32_t *code, size_t code_size)
{
    struct obj_member *member = get_obj_member(objs_array, code[1], code[2]);
    if (!member)
        return -1;

    const uint32_t decoration = code[3];
    const uint32_t extra = code_size > 4 * sizeof(*code) ? code[4] : 0;

    if (decoration == 35)
        member->offset = extra;

    return 0;
}

static const struct {
    int (*func)(struct darray *objs_array, const uint32_t *code, size_t code_size);
    int min_words;
} op_map[] = {
    [ 5] = {op_name,             3},
    [ 6] = {op_membername,       4},
    [22] = {op_typefloat,        3},
    [23] = {op_typevector,       4},
    [24] = {op_typematrix,       4},
    [25] = {op_typeimage,        9},
    [27] = {op_typesampledimage, 3},
    [30] = {op_typestruct,       2},
    [32] = {op_typepointer,      4},
    [59] = {op_variable,         4},
    [71] = {op_decorate,         3},
    [72] = {op_memberdecorate,   4},
};

static struct spirv_block *create_block(struct hmap *blocks, const char *key, const struct obj *obj)
{
    struct spirv_block *block = ngli_calloc(1, sizeof(*block));
    if (!block)
        return NULL;

    int ret = ngli_hmap_set(blocks, key, block);
    if (ret < 0) {
        ngli_free(block);
        return NULL;
    }

    ngli_darray_init(&block->members, sizeof(struct spirv_block_member), 0);
    block->size = obj->size;
    return block;
}

static void free_block(void *user_arg, void *arg)
{
    struct spirv_block *block = arg;
    struct darray *members_array = &block->members;
    struct spirv_block_member *members = ngli_darray_data(members_array);
    for (int i = 0; i < ngli_darray_count(members_array); i++)
        ngli_free(members[i].name);
    ngli_darray_reset(&block->members);
    ngli_free(block);
}

static int track_blocks(struct spirv_probe *s, struct darray *objs_array)
{
    s->blocks = ngli_hmap_create();
    if (!s->blocks)
        return -1;
    ngli_hmap_set_free(s->blocks, free_block, NULL);

    struct obj *objs = ngli_darray_data(objs_array);
    for (int i = 0; i < ngli_darray_count(objs_array); i++) {
        struct obj *obj = &objs[i];
        if (obj->type != NGLI_SPIRV_OBJECT_TYPE_STRUCT)
            continue;

        struct spirv_block *block = create_block(s->blocks, obj->name, obj);
        if (!block)
            return -1;

        const struct darray *members_array = &obj->members;
        for (int mb_idx = 0; mb_idx < ngli_darray_count(members_array); mb_idx++) {
            const struct obj_member *member = get_obj_member(objs_array, obj->id, mb_idx);
            if (!member)
                return -1;
            struct spirv_block_member *block_member = ngli_darray_push(&block->members, NULL);
            if (!block_member)
                return -1;
            block_member->offset = member->offset;
            block_member->name = ngli_strdup(member->name);
            block_member->type = member->type;
        }
    }

    return 0;
}

static struct spirv_variable *create_variable(struct hmap *variables, const char *key, const struct obj *obj)
{
    struct spirv_variable *variable = ngli_calloc(1, sizeof(*variable));
    if (!variable)
        return NULL;

    int ret = ngli_hmap_set(variables, key, variable);
    if (ret < 0) {
        ngli_free(variable);
        return NULL;
    }

    variable->descriptor_set = obj->descriptor_set;
    variable->binding        = obj->binding;
    variable->location       = obj->location;
    variable->storage_class  = obj->storage_class;
    return variable;
}

static void free_variable(void *user_arg, void *arg)
{
    struct spirv_variable *variable = arg;
    //ngli_free(variable->block_name);
    ngli_free(variable);
}

static int track_variables(struct spirv_probe *s, struct darray *objs_array)
{
    s->variables = ngli_hmap_create();
    if (!s->variables)
        return -1;
    ngli_hmap_set_free(s->variables, free_variable, NULL);

    const struct obj *objs = ngli_darray_data(objs_array);
    for (int i = 0; i < ngli_darray_count(objs_array); i++) {
        const struct obj *obj = &objs[i];
        if (obj->type != NGLI_SPIRV_OBJECT_TYPE_VARIABLE ||
            obj->storage_class == NGLI_SPIRV_STORAGE_CLASS_UNSUPPORTED)
            continue;

        struct obj *obj_target = get_obj(objs_array, obj->target);
        if (!obj_target)
            return -1;

        if (obj_target->type == NGLI_SPIRV_OBJECT_TYPE_STRUCT) {
#if 0
            struct darray *members_array = &obj_target->members;
            struct obj_member *members = ngli_darray_data(members_array);
            for (int mb_idx = 0; mb_idx < ngli_darray_count(members_array); mb_idx++) {
                const struct obj_member *member = &members[mb_idx];

                char *key = ngli_asprintf("%s%s%s", obj->name, *obj->name ? "." : "", member->name);
                if (!key)
                    return -1;

                struct spirv_variable *variable = create_variable(s->variables, key, obj);
                ngli_free(key);
                if (!variable)
                    return -1;

                variable->type        = member->type;

                //variable->block_member_index = member->index;
                //variable->block_name         = ngli_strdup(obj_target->name);
                //if (!variable->block_name) {
                //    free_variable(NULL, variable);
                //    return -1;
                //}
            }
#else
            struct spirv_variable *variable = create_variable(s->variables, obj->name, obj);
            if (!variable)
                return -1;

            variable->type = obj->target_type;
            //continue;
#endif
        } else {
            struct spirv_variable *variable = create_variable(s->variables, obj->name, obj);
            if (!variable)
                return -1;

            variable->type = obj->target_type;
        }
    }
    return 0;
}

struct spirv_probe *ngli_spirv_probe(const uint32_t *code, size_t size)
{
    if (size < sizeof(struct spirv_header))
        return NULL;

    const struct spirv_header *header = (struct spirv_header *)code;
    if (header->magic != 0x07230203)
        return NULL;
    if (header->version != 0x00010000) // XXX: allow more?
        return NULL;

    code += sizeof(*header) / sizeof(*code);
    size -= sizeof(*header);

    struct spirv_probe *s = ngli_calloc(1, sizeof(*s));
    if (!s)
        return NULL;

    struct darray objs_array;
    ngli_darray_init(&objs_array, sizeof(struct obj), 0);

    while (size > 0) {
        const uint32_t opcode0    = code[0];
        const uint16_t opcode     = opcode0 & 0xffff;
        const uint16_t word_count = opcode0 >> 16;

        const uint32_t instr_size = word_count * sizeof(*code);
        if (instr_size > size) {
            ngli_spirv_freep(&s);
            goto end;
        }

        if (opcode < NGLI_ARRAY_NB(op_map) && op_map[opcode].func) {
            if (word_count >= op_map[opcode].min_words) {
                int ret = op_map[opcode].func(&objs_array, code, instr_size);
                if (ret < 0) {
                    LOG(ERROR, "unable to handle opcode %d: %d", opcode, ret);
                    ngli_spirv_freep(&s);
                    goto end;
                }
            }
        }

        code += word_count;
        size -= instr_size;
    }

    if (track_blocks(s, &objs_array) < 0 ||
        track_variables(s, &objs_array) < 0)
        ngli_spirv_freep(&s);

end:
    free_objs(&objs_array);

    return s;
}

void ngli_spirv_freep(struct spirv_probe **probep)
{
    struct spirv_probe *s = *probep;

    if (!s)
        return;
    ngli_hmap_freep(&s->variables);
    ngli_hmap_freep(&s->blocks);
    ngli_free(s);
    *probep = NULL;
}
