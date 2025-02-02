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

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sxplayer.h>

#include <CoreVideo/CoreVideo.h>

#include "format.h"
#include "glincludes.h"
#include "hwconv.h"
#include "hwupload.h"
#include "log.h"
#include "math_utils.h"
#include "memory.h"
#include "nodegl.h"
#include "nodes.h"

#define NGLI_CFRELEASE(ref) do { \
    if (ref) {                   \
        CFRelease(ref);          \
        ref = NULL;              \
    }                            \
} while (0)

struct hwupload_vt_ios {
    struct hwconv hwconv;
    struct texture planes[2];
    int width;
    int height;
    OSType format;
    CVOpenGLESTextureRef ios_textures[2];
};

struct format_desc {
    int layout;
    int nb_planes;
    struct {
        int format;
    } planes[2];
};

static int vt_get_format_desc(OSType format, struct format_desc *desc)
{
    switch (format) {
    case kCVPixelFormatType_32BGRA:
        desc->layout = NGLI_IMAGE_LAYOUT_DEFAULT;
        desc->nb_planes = 1;
        desc->planes[0].format = NGLI_FORMAT_B8G8R8A8_UNORM;
        break;
    case kCVPixelFormatType_32RGBA:
        desc->layout = NGLI_IMAGE_LAYOUT_DEFAULT;
        desc->nb_planes = 1;
        desc->planes[0].format = NGLI_FORMAT_R8G8B8A8_UNORM;
        break;
    case kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange:
        desc->layout = NGLI_IMAGE_LAYOUT_NV12;
        desc->nb_planes = 2;
        desc->planes[0].format = NGLI_FORMAT_R8_UNORM;
        desc->planes[1].format = NGLI_FORMAT_R8G8_UNORM;
        break;
    default:
        return -1;
    }

    return 0;
}

static int vt_ios_common_map_plane(struct ngl_node *node, CVPixelBufferRef cvpixbuf, int index)
{
    struct ngl_ctx *ctx = node->ctx;
    struct glcontext *gl = ctx->glcontext;
    struct texture_priv *s = node->priv_data;
    struct hwupload_vt_ios *vt = s->hwupload_priv_data;
    struct texture *plane = &vt->planes[index];
    const struct texture_params *plane_params = &plane->params;

    NGLI_CFRELEASE(vt->ios_textures[index]);

    int width  = CVPixelBufferGetWidthOfPlane(cvpixbuf, index);
    int height = CVPixelBufferGetHeightOfPlane(cvpixbuf, index);

    CVOpenGLESTextureCacheRef *cache = ngli_glcontext_get_texture_cache(gl);

    CVReturn err = CVOpenGLESTextureCacheCreateTextureFromImage(kCFAllocatorDefault,
                                                                *cache,
                                                                cvpixbuf,
                                                                NULL,
                                                                GL_TEXTURE_2D,
                                                                plane->internal_format,
                                                                width,
                                                                height,
                                                                plane->format,
                                                                plane->format_type,
                                                                index,
                                                                &vt->ios_textures[index]);
    if (err != noErr) {
        LOG(ERROR, "could not create CoreVideo texture from image: %d", err);
        return -1;
    }

    GLint id = CVOpenGLESTextureGetName(vt->ios_textures[index]);
    const GLint min_filter = ngli_texture_get_gl_min_filter(plane_params->min_filter, plane_params->mipmap_filter);
    const GLint mag_filter = ngli_texture_get_gl_mag_filter(plane_params->mag_filter);
    const GLint wrap_s = ngli_texture_get_gl_wrap(plane_params->wrap_s);
    const GLint wrap_t = ngli_texture_get_gl_wrap(plane_params->wrap_t);

    ngli_glBindTexture(gl, GL_TEXTURE_2D, id);
    ngli_glTexParameteri(gl, GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter);
    ngli_glTexParameteri(gl, GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_filter);
    ngli_glTexParameteri(gl, GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_s);
    ngli_glTexParameteri(gl, GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_t);
    ngli_glBindTexture(gl, GL_TEXTURE_2D, 0);

    ngli_texture_set_id(plane, id);
    ngli_texture_set_dimensions(plane, width, height, 0);

    return 0;
}

static int vt_ios_common_map_frame(struct ngl_node *node, struct sxplayer_frame *frame)
{
    struct texture_priv *s = node->priv_data;
    struct hwupload_vt_ios *vt = s->hwupload_priv_data;

    CVPixelBufferRef cvpixbuf = (CVPixelBufferRef)frame->data;
    OSType cvformat = CVPixelBufferGetPixelFormatType(cvpixbuf);
    ngli_assert(vt->format == cvformat);

    vt->width  = CVPixelBufferGetWidth(cvpixbuf);
    vt->height = CVPixelBufferGetHeight(cvpixbuf);

    int ret;
    switch (vt->format) {
    case kCVPixelFormatType_32BGRA:
    case kCVPixelFormatType_32RGBA:
        ret = vt_ios_common_map_plane(node, cvpixbuf, 0);
        if (ret < 0)
            return ret;
        break;
    case kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange: {
        ret = vt_ios_common_map_plane(node, cvpixbuf, 0);
        if (ret < 0)
            return ret;
        ret = vt_ios_common_map_plane(node, cvpixbuf, 1);
        if (ret < 0)
            return ret;
        break;
    }
    default:
        ngli_assert(0);
    }

    return 0;
}

static void vt_ios_common_uninit(struct ngl_node *node)
{
    struct texture_priv *s = node->priv_data;
    struct hwupload_vt_ios *vt = s->hwupload_priv_data;

    ngli_hwconv_reset(&vt->hwconv);
    ngli_texture_reset(&s->texture);

    ngli_texture_reset(&vt->planes[0]);
    ngli_texture_reset(&vt->planes[1]);

    NGLI_CFRELEASE(vt->ios_textures[0]);
    NGLI_CFRELEASE(vt->ios_textures[1]);
}

static int vt_ios_init(struct ngl_node *node, struct sxplayer_frame *frame)
{
    struct ngl_ctx *ctx = node->ctx;
    struct texture_priv *s = node->priv_data;
    struct hwupload_vt_ios *vt = s->hwupload_priv_data;

    CVPixelBufferRef cvpixbuf = (CVPixelBufferRef)frame->data;
    vt->format = CVPixelBufferGetPixelFormatType(cvpixbuf);
    ngli_assert(vt->format == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange);

    vt->width  = CVPixelBufferGetWidth(cvpixbuf);
    vt->height = CVPixelBufferGetHeight(cvpixbuf);

    struct format_desc format_desc = {0};
    int ret = vt_get_format_desc(vt->format, &format_desc);
    if (ret < 0)
        return ret;

    for (int i = 0; i < format_desc.nb_planes; i++) {
        struct texture *plane = &vt->planes[i];
        struct texture_params plane_params = NGLI_TEXTURE_PARAM_DEFAULTS;
        plane_params.format = format_desc.planes[i].format;

        ret = ngli_texture_wrap(plane, ctx, &plane_params, 0);
        if (ret < 0)
            return ret;
    }

    struct texture_params params = s->params;
    params.format = NGLI_FORMAT_B8G8R8A8_UNORM;
    params.width  = vt->width;
    params.height = vt->height;

    ret = ngli_texture_init(&s->texture, ctx, &params);
    if (ret < 0)
        return ret;

    ret = ngli_hwconv_init(&vt->hwconv, ctx, &s->texture, NGLI_IMAGE_LAYOUT_NV12);
    if (ret < 0)
        return ret;

    ngli_image_init(&s->image, NGLI_IMAGE_LAYOUT_DEFAULT, &s->texture);

    return 0;
}

static int vt_ios_map_frame(struct ngl_node *node, struct sxplayer_frame *frame)
{
    struct texture_priv *s = node->priv_data;
    struct hwupload_vt_ios *vt = s->hwupload_priv_data;

    int ret = vt_ios_common_map_frame(node, frame);
    if (ret < 0)
        return ret;

    if (!ngli_texture_match_dimensions(&s->texture, vt->width, vt->height, 0)) {
        struct ngl_ctx *ctx = node->ctx;

        ngli_hwconv_reset(&vt->hwconv);
        ngli_texture_reset(&s->texture);

        struct texture_params params = s->params;
        params.format = NGLI_FORMAT_B8G8R8A8_UNORM;
        params.width  = vt->width;
        params.height = vt->height;

        ret = ngli_texture_init(&s->texture, ctx, &params);
        if (ret < 0)
            return ret;

        ret = ngli_hwconv_init(&vt->hwconv, ctx, &s->texture, NGLI_IMAGE_LAYOUT_NV12);
        if (ret < 0)
            return ret;
    }

    ret = ngli_hwconv_convert(&vt->hwconv, vt->planes, NULL);
    if (ret < 0)
        return ret;

    NGLI_CFRELEASE(vt->ios_textures[0]);
    NGLI_CFRELEASE(vt->ios_textures[1]);

    if (ngli_texture_has_mipmap(&s->texture))
        ngli_texture_generate_mipmap(&s->texture);

    return 0;
}

static int vt_ios_dr_init(struct ngl_node *node, struct sxplayer_frame *frame)
{
    struct ngl_ctx *ctx = node->ctx;
    struct texture_priv *s = node->priv_data;
    struct hwupload_vt_ios *vt = s->hwupload_priv_data;

    CVPixelBufferRef cvpixbuf = (CVPixelBufferRef)frame->data;
    vt->format = CVPixelBufferGetPixelFormatType(cvpixbuf);

    struct texture_params plane_params = s->params;
    if (plane_params.mipmap_filter) {
        LOG(WARNING, "IOSurface RGBA/BGRA buffers do not support mipmapping: "
            "disabling mipmapping");
        plane_params.mipmap_filter = NGLI_MIPMAP_FILTER_NONE;
    }

    struct format_desc format_desc = {0};
    int ret = vt_get_format_desc(vt->format, &format_desc);
    if (ret < 0)
        return ret;

    for (int i = 0; i < format_desc.nb_planes; i++) {
        struct texture *plane = &vt->planes[i];

        plane_params.format = format_desc.planes[i].format;
        ret = ngli_texture_wrap(plane, ctx, &plane_params, 0);
        if (ret < 0)
            return ret;
    }

    ngli_image_init(&s->image, format_desc.layout, &vt->planes[0], &vt->planes[1]);

    return 0;
}

static const struct hwmap_class hwmap_vt_ios_class = {
    .name      = "videotoolbox (nv12 → rgba)",
    .priv_size = sizeof(struct hwupload_vt_ios),
    .init      = vt_ios_init,
    .map_frame = vt_ios_map_frame,
    .uninit    = vt_ios_common_uninit,
};

static const struct hwmap_class hwmap_vt_ios_dr_class = {
    .name      = "videotoolbox (zero-copy)",
    .priv_size = sizeof(struct hwupload_vt_ios),
    .init      = vt_ios_dr_init,
    .map_frame = vt_ios_common_map_frame,
    .uninit    = vt_ios_common_uninit,
};

static const struct hwmap_class *vt_ios_get_hwmap(struct ngl_node *node, struct sxplayer_frame *frame)
{
    struct texture_priv *s = node->priv_data;

    CVPixelBufferRef cvpixbuf = (CVPixelBufferRef)frame->data;
    OSType cvformat = CVPixelBufferGetPixelFormatType(cvpixbuf);
    int direct_rendering = s->supported_image_layouts & (1 << NGLI_IMAGE_LAYOUT_NV12);

    switch (cvformat) {
    case kCVPixelFormatType_32BGRA:
    case kCVPixelFormatType_32RGBA:
        return &hwmap_vt_ios_dr_class;
    case kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange:
        if (direct_rendering && s->params.mipmap_filter) {
            LOG(WARNING, "IOSurface NV12 buffers do not support mipmapping: "
                "disabling direct rendering");
            direct_rendering = 0;
        }

        return direct_rendering ? &hwmap_vt_ios_dr_class : &hwmap_vt_ios_class;
    default:
        return NULL;
    }
}

const struct hwupload_class ngli_hwupload_vt_ios_class = {
    .get_hwmap = vt_ios_get_hwmap,
};
