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

#ifndef BLOCK_H
#define BLOCK_H

#include <stdint.h>

#include "glcontext.h"
#include "variable.h"

enum {
    NGLI_BLOCK_LAYOUT_STD140,
    NGLI_BLOCK_LAYOUT_STD430,
    NGLI_BLOCK_NB_LAYOUTS
};

struct block_field_info {
    int spec_id;
    int offset;
    int size;
    int stride;
};

struct block {
    struct glcontext *gl;
    const struct variable *fields;
    struct block_field_info *field_info;
    int nb_field_info;
    uint8_t *data;
    int data_size;
    int usage;
};

int ngli_block_init(struct block *block, struct glcontext *gl, int nb_fields, const struct variable *fields);
void ngli_block_reset(struct block *block);

#endif
