#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sxplayer.h>

#include <va/va_x11.h>
#include <va/va_drmcommon.h>

#include "egl.h"
#include "glincludes.h"
#include "hwconv.h"
#include "hwupload.h"
#include "image.h"
#include "log.h"
#include "nodegl.h"
#include "nodes.h"
#include "utils.h"

struct hwupload_vaapi {
    struct sxplayer_frame *frame;
    struct hwconv hwconv;
    struct texture planes[2];

    EGLImageKHR egl_images[2];

    VADRMPRIMESurfaceDescriptor surface_descriptor;
    int surface_acquired;
};

static int vaapi_common_init(struct ngl_node *node, struct sxplayer_frame *frame)
{
    struct ngl_ctx *ctx = node->ctx;
    struct glcontext *gl = ctx->glcontext;
    struct texture_priv *s = node->priv_data;
    struct hwupload_vaapi *vaapi = s->hwupload_priv_data;

    if (!(gl->features & (NGLI_FEATURE_OES_EGL_IMAGE |
                          NGLI_FEATURE_EGL_IMAGE_BASE_KHR |
                          NGLI_FEATURE_EGL_EXT_IMAGE_DMA_BUF_IMPORT))) {
        LOG(ERROR, "context does not support required extensions for vaapi");
        return -1;
    }

    for (int i = 0; i < 2; i++) {
        const struct texture_params *params = &s->params;

        int format = i == 0 ? NGLI_FORMAT_R8_UNORM : NGLI_FORMAT_R8G8_UNORM;

        struct texture *plane = &vaapi->planes[i];
        const struct texture_params plane_params = {
            .dimensions = 2,
            .format = format,
            .min_filter = params->min_filter,
            .mag_filter = params->mag_filter,
            .mipmap_filter = NGLI_MIPMAP_FILTER_NONE,
            .wrap_s = params->wrap_s,
            .wrap_t = params->wrap_t,
            .wrap_r = params->wrap_r,
            .access = params->access,
            .external_storage = 1,
        };

        int ret = ngli_texture_init(plane, ctx, &plane_params);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static void vaapi_common_uninit(struct ngl_node *node)
{
    struct ngl_ctx *ctx = node->ctx;
    struct glcontext *gl = ctx->glcontext;
    struct texture_priv *s = node->priv_data;
    struct hwupload_vaapi *vaapi = s->hwupload_priv_data;

    for (int i = 0; i < 2; i++)
        ngli_texture_reset(&vaapi->planes[i]);

    if (vaapi->surface_acquired) {
        for (int i = 0; i < 2; i++) {
            if (vaapi->egl_images[i]) {
                ngli_eglDestroyImageKHR(gl, vaapi->egl_images[i]);
                vaapi->egl_images[i] = NULL;
            }
        }
        for (int i = 0; i < vaapi->surface_descriptor.num_objects; i++) {
            close(vaapi->surface_descriptor.objects[i].fd);
        }
        vaapi->surface_acquired = 0;
    }

    ngli_hwconv_reset(&vaapi->hwconv);
    ngli_texture_reset(&s->texture);

    sxplayer_release_frame(vaapi->frame);
    vaapi->frame = NULL;
}

static int vaapi_common_map_frame(struct ngl_node *node, struct sxplayer_frame *frame)
{
    struct ngl_ctx *ctx = node->ctx;
    struct glcontext *gl = ctx->glcontext;
    struct texture_priv *s = node->priv_data;
    struct hwupload_vaapi *vaapi = s->hwupload_priv_data;

    sxplayer_release_frame(vaapi->frame);
    vaapi->frame = frame;

    if (vaapi->surface_acquired) {
        for (int i = 0; i < 2; i++) {
            if (vaapi->egl_images[i]) {
                ngli_eglDestroyImageKHR(gl, vaapi->egl_images[i]);
                vaapi->egl_images[i] = NULL;
            }
        }
        for (int i = 0; i < vaapi->surface_descriptor.num_objects; i++) {
            close(vaapi->surface_descriptor.objects[i].fd);
        }
        vaapi->surface_acquired = 0;
    }

    VASurfaceID surface_id = (VASurfaceID)(intptr_t)frame->data;
    VAStatus status = vaExportSurfaceHandle(ctx->va_display,
                                            surface_id,
                                            VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
                                            VA_EXPORT_SURFACE_READ_ONLY |
                                            VA_EXPORT_SURFACE_SEPARATE_LAYERS,
                                            &vaapi->surface_descriptor);
    if (status != VA_STATUS_SUCCESS) {
        LOG(ERROR, "failed to export vaapi surface handle: 0x%x", status);
        return -1;
    }
    vaapi->surface_acquired = 1;

    if (vaapi->surface_descriptor.fourcc != VA_FOURCC_NV12 &&
        vaapi->surface_descriptor.fourcc != VA_FOURCC_P010 &&
        vaapi->surface_descriptor.fourcc != VA_FOURCC_P016) {
        LOG(ERROR, "unsupported vaapi surface format: 0x%x", vaapi->surface_descriptor.fourcc);
        return -1;
    }

    int num_layers = vaapi->surface_descriptor.num_layers;
    if (num_layers > NGLI_ARRAY_NB(vaapi->egl_images)) {
        LOG(WARNING, "vaapi layer count (%d) exceeds plane count (%d)", num_layers, NGLI_ARRAY_NB(vaapi->egl_images));
        num_layers = NGLI_ARRAY_NB(vaapi->egl_images);
    }

    for (int i = 0; i < num_layers; i++) {
        int attribs[32] = {EGL_NONE};
        int nb_attribs = 0;

#define ADD_ATTRIB(name, value) do {                          \
    ngli_assert(nb_attribs + 3 < NGLI_ARRAY_NB(attribs));     \
    attribs[nb_attribs++] = (name);                           \
    attribs[nb_attribs++] = (value);                          \
    attribs[nb_attribs] = EGL_NONE;                           \
} while(0)

#define ADD_PLANE_ATTRIBS(plane) do {                                                \
    uint32_t object_index = vaapi->surface_descriptor.layers[i].object_index[plane]; \
    ADD_ATTRIB(EGL_DMA_BUF_PLANE ## plane ## _FD_EXT,                                \
               vaapi->surface_descriptor.objects[object_index].fd);                  \
    ADD_ATTRIB(EGL_DMA_BUF_PLANE ## plane ## _OFFSET_EXT,                            \
               vaapi->surface_descriptor.layers[i].offset[plane]);                   \
    ADD_ATTRIB(EGL_DMA_BUF_PLANE ## plane ## _PITCH_EXT,                             \
               vaapi->surface_descriptor.layers[i].pitch[plane]);                    \
} while (0)

        int width = i == 0 ? frame->width : (frame->width + 1) >> 1;
        int height = i == 0 ? frame->height : (frame->height + 1) >> 1;

        ADD_ATTRIB(EGL_LINUX_DRM_FOURCC_EXT, vaapi->surface_descriptor.layers[i].drm_format);
        ADD_ATTRIB(EGL_WIDTH,  width);
        ADD_ATTRIB(EGL_HEIGHT, height);

        ADD_PLANE_ATTRIBS(0);
        if (vaapi->surface_descriptor.layers[i].num_planes > 1)
            ADD_PLANE_ATTRIBS(1);
        if (vaapi->surface_descriptor.layers[i].num_planes > 2)
            ADD_PLANE_ATTRIBS(2);
        if (vaapi->surface_descriptor.layers[i].num_planes > 3)
            ADD_PLANE_ATTRIBS(3);

        vaapi->egl_images[i] = ngli_eglCreateImageKHR(gl,
                                                      EGL_NO_CONTEXT,
                                                      EGL_LINUX_DMA_BUF_EXT,
                                                      NULL,
                                                      attribs);
        if (!vaapi->egl_images[i]) {
            LOG(ERROR, "failed to create egl image");
            return -1;
        }

        struct texture *plane = &vaapi->planes[i];
        ngli_texture_set_dimensions(plane, width, height, 0);

        ngli_glBindTexture(gl, plane->target, plane->id);
        ngli_glEGLImageTargetTexture2DOES(gl, plane->target, vaapi->egl_images[i]);
    }

    return 0;
}

static int vaapi_init(struct ngl_node *node, struct sxplayer_frame *frame)
{
    struct ngl_ctx *ctx = node->ctx;
    struct texture_priv *s = node->priv_data;
    struct hwupload_vaapi *vaapi = s->hwupload_priv_data;

    int ret = vaapi_common_init(node, frame);
    if (ret < 0)
        return ret;

    struct texture_params params = s->params;
    params.format = NGLI_FORMAT_R8G8B8A8_UNORM;
    params.width  = frame->width;
    params.height = frame->height;

    ret = ngli_texture_init(&s->texture, ctx, &params);
    if (ret < 0)
        return ret;

    ret = ngli_hwconv_init(&vaapi->hwconv, ctx, &s->texture, NGLI_IMAGE_LAYOUT_NV12);
    if (ret < 0)
        return ret;

    ngli_image_init(&s->image, NGLI_IMAGE_LAYOUT_DEFAULT, &s->texture);

    return 0;
}

static int vaapi_map_frame(struct ngl_node *node, struct sxplayer_frame *frame)
{
    struct texture_priv *s = node->priv_data;
    struct hwupload_vaapi *vaapi = s->hwupload_priv_data;

    int ret = vaapi_common_map_frame(node, frame);
    if (ret < 0)
        return ret;

    if (!ngli_texture_match_dimensions(&s->texture, frame->width, frame->height, 0)) {
        struct ngl_ctx *ctx = node->ctx;

        ngli_hwconv_reset(&vaapi->hwconv);
        ngli_texture_reset(&s->texture);

        struct texture_params params = s->params;
        params.format = NGLI_FORMAT_R8G8B8A8_UNORM;
        params.width  = frame->width;
        params.height = frame->height;

        ret = ngli_texture_init(&s->texture, ctx, &params);
        if (ret < 0)
            return ret;

        ret = ngli_hwconv_init(&vaapi->hwconv, ctx, &s->texture, NGLI_IMAGE_LAYOUT_NV12);
        if (ret < 0)
            return ret;
    }

    ret = ngli_hwconv_convert(&vaapi->hwconv, vaapi->planes, NULL);
    if (ret < 0)
        return ret;

    if (ngli_texture_has_mipmap(&s->texture))
        ngli_texture_generate_mipmap(&s->texture);

    return 0;
}

static int vaapi_dr_init(struct ngl_node *node, struct sxplayer_frame *frame)
{
    struct texture_priv *s = node->priv_data;
    struct hwupload_vaapi *vaapi = s->hwupload_priv_data;

    int ret = vaapi_common_init(node, frame);
    if (ret < 0)
        return ret;

    ngli_image_init(&s->image, NGLI_IMAGE_LAYOUT_NV12, &vaapi->planes[0], &vaapi->planes[1]);

    return 0;
}

static const struct hwmap_class hwmap_vaapi_class = {
    .name      = "vaapi (dma buf → egl image → rgba)",
    .flags     = HWMAP_FLAG_FRAME_OWNER,
    .priv_size = sizeof(struct hwupload_vaapi),
    .init      = vaapi_init,
    .map_frame = vaapi_map_frame,
    .uninit    = vaapi_common_uninit,
};

static const struct hwmap_class hwmap_vaapi_dr_class = {
    .name      = "vaapi (dma buf → egl image)",
    .flags     = HWMAP_FLAG_FRAME_OWNER,
    .priv_size = sizeof(struct hwupload_vaapi),
    .init      = vaapi_dr_init,
    .map_frame = vaapi_common_map_frame,
    .uninit    = vaapi_common_uninit,
};

static const struct hwmap_class *vaapi_get_hwmap(struct ngl_node *node, struct sxplayer_frame *frame)
{
    struct texture_priv *s = node->priv_data;
    int direct_rendering = s->supported_image_layouts & (1 << NGLI_IMAGE_LAYOUT_NV12);

    if (direct_rendering && s->params.mipmap_filter) {
        LOG(WARNING,
            "vaapi direct rendering does not support mipmapping: "
            "disabling direct rendering");
        direct_rendering = 0;
    }

    return direct_rendering ? &hwmap_vaapi_dr_class : &hwmap_vaapi_class;
}

const struct hwupload_class ngli_hwupload_vaapi_class = {
    .get_hwmap = vaapi_get_hwmap,
};
