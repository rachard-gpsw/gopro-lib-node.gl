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

#ifndef BUFFER_H
#define BUFFER_H

#include "glincludes.h"

enum {
    NGLI_BUFFER_USAGE_STATIC,
    NGLI_BUFFER_USAGE_DYNAMIC,
    NGLI_BUFFER_USAGE_NB
};

struct buffer {
    struct ngl_ctx *ctx;
    int size;
    int usage;
    GLuint id;
};

int ngli_buffer_init(struct buffer *s, struct ngl_ctx *ctx, int size, int usage);
int ngli_buffer_upload(struct buffer *s, const void *data, int size);
void ngli_buffer_reset(struct buffer *s);

#endif
