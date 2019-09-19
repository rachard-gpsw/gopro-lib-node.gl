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

#ifndef HWCONV_H
#define HWCONV_H

#include "buffer.h"
#include "rendertarget.h"
#include "glincludes.h"
#include "glcontext.h"
#include "image.h"
#include "program.h"
#include "texture.h"
#include "pipeline.h"

struct hwconv {
    struct ngl_ctx *ctx;
    enum image_layout src_layout;
    NGLI_ALIGNED_MAT(src_color_matrix);

    struct rendertarget rt;
    struct texture color_attachment;
    struct program program;
    struct buffer vertices;
    struct pipeline pipeline;

    int tex_indices[2];
    int tex_coord_matrix_index;
    int tex_dimensions_index;
};

int ngli_hwconv_init(struct hwconv *hwconv, struct ngl_ctx *ctx,
                     const struct texture *dst_texture,
                     enum image_layout src_layout);

int ngli_hwconv_convert(struct hwconv *hwconv, struct texture *planes, const float *matrix);
void ngli_hwconv_reset(struct hwconv *texconv);

#endif
