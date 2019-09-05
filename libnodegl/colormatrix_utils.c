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

#include <sxplayer.h>

#include "log.h"
#include "utils.h"
#include "colormatrix.h"
#include "colormatrix_utils.h"

static const char * sxplayer_col_spc_str[] = {
    [SXPLAYER_COL_SPC_RGB]                = "rgb",
    [SXPLAYER_COL_SPC_BT709]              = "bt709",
    [SXPLAYER_COL_SPC_UNSPECIFIED]        = "unspecified",
    [SXPLAYER_COL_SPC_RESERVED]           = "reserved",
    [SXPLAYER_COL_SPC_FCC]                = "fcc",
    [SXPLAYER_COL_SPC_BT470BG]            = "bt470bg",
    [SXPLAYER_COL_SPC_SMPTE170M]          = "smpte170m",
    [SXPLAYER_COL_SPC_SMPTE240M]          = "smpte240m",
    [SXPLAYER_COL_SPC_YCGCO]              = "ycgco",
    [SXPLAYER_COL_SPC_BT2020_NCL]         = "bt2020_ncl",
    [SXPLAYER_COL_SPC_BT2020_CL]          = "bt2020_cl",
    [SXPLAYER_COL_SPC_SMPTE2085]          = "smpte2085",
    [SXPLAYER_COL_SPC_CHROMA_DERIVED_NCL] = "chroma_derived_ncl",
    [SXPLAYER_COL_SPC_CHROMA_DERIVED_CL]  = "chroma_derived_cl",
    [SXPLAYER_COL_SPC_ICTCP]              = "ictcp",
};

static const int color_space_map[] = {
    [SXPLAYER_COL_SPC_BT470BG]    = NGLI_COLORMATRIX_BT601,
    [SXPLAYER_COL_SPC_SMPTE170M]  = NGLI_COLORMATRIX_BT601,
    [SXPLAYER_COL_SPC_BT709]      = NGLI_COLORMATRIX_BT709,
    [SXPLAYER_COL_SPC_BT2020_NCL] = NGLI_COLORMATRIX_BT2020,
    [SXPLAYER_COL_SPC_BT2020_CL]  = NGLI_COLORMATRIX_BT2020,
};

NGLI_STATIC_ASSERT(undefined_col_is_zero, NGLI_COLORMATRIX_UNDEFINED == 0);

static const char *get_col_spc_str(int color_space)
{
    if (color_space < 0 || color_space >= NGLI_ARRAY_NB(sxplayer_col_spc_str))
        return NULL;
    return sxplayer_col_spc_str[color_space];
}

#define DEFAULT_COLORMATRIX NGLI_COLORMATRIX_BT709

static int unsupported_colormatrix(int color_space)
{
    const char *colormatrix_str = get_col_spc_str(color_space);
    if (!colormatrix_str)
        LOG(WARNING, "unsupported colormatrix %d, fallback on default", color_space);
    else
        LOG(WARNING, "unsupported colormatrix %s, fallback on default", colormatrix_str);
    return DEFAULT_COLORMATRIX;
}

int ngli_get_colormatrix_from_sxplayer(int color_space)
{
    if (color_space == SXPLAYER_COL_SPC_UNSPECIFIED) {
        LOG(INFO, "media colormatrix unspecified, fallback on default matrix");
        return DEFAULT_COLORMATRIX;
    }

    if (color_space < 0 || color_space >= NGLI_ARRAY_NB(color_space_map))
        return unsupported_colormatrix(color_space);

    const int colormatrix = color_space_map[color_space];
    if (colormatrix == NGLI_COLORMATRIX_UNDEFINED)
        return unsupported_colormatrix(color_space);

    return color_space;
}
