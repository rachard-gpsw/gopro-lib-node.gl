#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
# Copyright 2019 GoPro Inc.
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#

from fractions import Fraction


'''
Video range constants:
    Yoff    = 16
    Yrange  = 219
    UVrange = 224

Full range constants:
    Yoff    = 0
    Yrange  = 255
    UVrange = 255

8-bit quantized Y,Cb,Cr:

    Yq  = Round((Y *255 - Yoff) / Yrange)
    Cbq = Round((Cb*255 - 128) / UVrange)
    Crq = Round((Cr*255 - 128) / UVrange)

BT.709 formula (deciphered from the constants in the paper):

    Yq  = Kr*R + Kg*G + Kb*B
    Cbq = (B-Yq) / (2*(1-Kb))
    Crq = (R-Yq) / (2*(1-Kr))

Inverting the equations to get R,G,B:

    R = Yq + Crq * 2*(1-Kr)
    G = Yq/Kg - Kr*R/Kg - Kb*B/Kg
    B = Yq + Cbq * 2*(1-Kb)

Expressing them according to Y,Cr,Cb (WARNING: removing quantization rounding):

    R = Yq + Crq * 2*(1-Kr)
      = (Y * 255 - Yoff) / Yrange + (Cr*255 - 128) * 2*(1-Kr) / UVrange
      = Y * 255/Yrange - Yoff/Yrange + Cr * 255*2*(1-Kr)/UVrange - 128*2*(1-Kr)/UVrange
      = Y * 255/Yrange + Cr * 255*2*(1-Kr)/UVrange + (-Yoff/Yrange - 128*2*(1-Kr)/UVrange)
            `---.----'        `--------.---------'   `-----------------.-----------------'
             Y factor              Cr factor                        Offset

    G = Yq/Kg - Kr*R/Kg - Kb*B/Kg
      = Yq/Kg - Kr*(Yq + Crq * 2*(1-Kr))/Kg - Kb*(Yq + Cbq * 2*(1-Kb))/Kg
      = Yq/Kg - Kr*Yq/Kg - Crq * Kr*2*(1-Kr)/Kg - Kb*Yq/Kg - Cbq * Kb*2*(1-Kb)/Kg
      = Yq/Kg - Kr*Yq/Kg - Kb*Yq/Kg - Cbq * Kb*2*(1-Kb)/Kg - Crq * Kr*2*(1-Kr)/Kg
      = Yq*(1 - Kr - Kb)/Kg + Cbq * -Kb*2*(1-Kb)/Kg + Crq * -Kr*2*(1-Kr)/Kg
      = (Y * 255 - Yoff) * (1 - Kr - Kb)/Kg / Yrange + Cbq * -Kb*2*(1-Kb)/Kg + Crq * -Kr*2*(1-Kr)/Kg
      = Y * 255*(1-Kr-Kb)/(Yrange*Kg) + Cbq * -Kb*2*(1-Kb)/Kg + Crq * -Kr*2*(1-Kr)/Kg - Yoff*(1-Kr-Kb)/(Yrange*Kg)
      = Y * 255*(1-Kr-Kb)/(Yrange*Kg) + (Cb*255 - 128) / UVrange * -Kb*2*(1-Kb)/Kg + (Cr*255 - 128) / UVrange * -Kr*2*(1-Kr)/Kg - Yoff*(1-Kr-Kb)/(Yrange*Kg)
      = Y * 255*(1-Kr-Kb)/(Yrange*Kg) + Cb * 255*-Kb*2*(1-Kb)/Kg/UVrange + Cr * 255*-Kr*2*(1-Kr)/Kg/UVrange - (Yoff*(1-Kr-Kb)/(Yrange*Kg) + 128*-Kb*2*(1-Kb)/Kg/UVrange + 128*-Kr*2*(1-Kr)/Kg/UVrange)
      = Y * 255*(1-Kr-Kb)/(Yrange*Kg) + Cb * -255*Kb*2*(1-Kb)/(Kg*UVrange) + Cr * -255*Kr*2*(1-Kr)/(Kg*UVrange) + (-Yoff*(1-Kr-Kb)/Yrange + 128*Kb*2*(1-Kb)/UVrange + 128*Kr*2*(1-Kr)/UVrange)/Kg
            `----------.------------'        `-------------.-------------'        `------------.--------------'   `-------------------------------------.---------------------------------------'
                   Y factor                           Cb factor                            Cr factor                                                   Offset

    B = Yq + Cbq * 2*(1-Kb)
      = <similarly to R>
      = Y * 255/Yrange + Cb * 255*2*(1-Kb)/UVrange + (-Yoff/Yrange - 128*2*(1-Kr)/UVrange)
            `---.----'        `--------.---------'   `-----------------.-----------------'
             Y factor              Cb factor                        Offset
'''


K_constants = dict(
    bt601=(
        Fraction(299, 1000),
        Fraction(587, 1000),
        Fraction(114, 1000),
    ),
    bt709=(
        Fraction(2126, 10000),
        Fraction(7152, 10000),
        Fraction( 722, 10000),
    ),
    bt2020=(
        Fraction(2627, 10000),
        Fraction(6780, 10000),
        Fraction( 593, 10000),
    ),
)


def _get_matrix(name, video_range=True):
    Kr, Kg, Kb = K_constants[name]
    assert Kg == 1 - Kb - Kr
    y_off, y_range, uv_range = (16, 219, 224) if video_range else (0, 255, 255)

    r_y_factor  = 255 * Fraction(1, y_range)
    r_cb_factor = 0
    r_cr_factor = 255*2 * Fraction(1-Kr, uv_range)
    r_off       = Fraction(-y_off, y_range) - 128 * 2 * Fraction(1-Kr, uv_range)

    g_y_factor  = 255*Fraction(1-Kr-Kb, y_range*Kg)
    g_cb_factor = -255*Kb*2*Fraction(1-Kb, Kg*uv_range)
    g_cr_factor = -255*Kr*2*Fraction(1-Kr, Kg*uv_range)
    g_off       = Fraction(-y_off*Fraction(1-Kr-Kb, y_range) + 128*Kb*2*Fraction(1-Kb, uv_range) + 128*Kr*2*Fraction(1-Kr, uv_range), Kg)

    b_y_factor  = 255 * Fraction(1, y_range)
    b_cb_factor = 255*2 * Fraction(1-Kb, uv_range)
    b_cr_factor = 0
    b_off       = Fraction(-y_off, y_range) - 128 * 2 * Fraction(1-Kb, uv_range)

    return [
        [r_y_factor,  g_y_factor,  b_y_factor,  Fraction(0)],
        [r_cb_factor, g_cb_factor, b_cb_factor, Fraction(0)],
        [r_cr_factor, g_cr_factor, b_cr_factor, Fraction(0)],
        [r_off,       g_off,       b_off,       Fraction(1)],
    ]


_matrix_tpl = '''    [{}] = {{
        {:>16}., {:>20}., {:>16}., {}.,
        {:>16}., {:>20}., {:>16}., {}.,
        {:>16}., {:>20}., {:>16}., {}.,
        {:>16}., {:>20}., {:>16}., {}.,
    }},'''

_enum_tpl = '#define {:<26} {}'

_file_c_tpl = '''
/* DO NOT EDIT - This file is autogenerated */

#include <string.h>

#include "{}"

static const float colormatrices[][16] = {{
{}
}};

void ngli_colormatrix_yuv_to_rgb(float *dst, int colormatrix)
{{
    memcpy(dst, colormatrices[colormatrix], sizeof(colormatrices[colormatrix]));
}}
'''

_file_h_tpl = '''
/* DO NOT EDIT - This file is autogenerated */

#ifndef COLORMATRIX_H
#define COLORMATRIX_H

{}

void ngli_colormatrix_yuv_to_rgb(float *dst, int colormatrix);

#endif
'''


def _gen_tables(file_h, file_c):
    cspaces = ("undefined", "bt601", "bt709", "bt2020")

    enums, matrices = [], []

    for i, cspace in enumerate(cspaces):
        enums.append('NGLI_COLORMATRIX_' + cspace.upper())
        if i == 0:
            continue
        mat = _get_matrix(cspace)
        flat_mat = [x for row in mat for x in row]
        matrices.append(flat_mat)

    enum_defines = '\n'.join(_enum_tpl.format(enum, i) for i, enum in enumerate(enums))
    matrix_defs = '\n'.join(_matrix_tpl.format(enum, *mat) for enum, mat in zip(enums[1:], matrices))

    open(file_h, 'w').write(_file_h_tpl.format(enum_defines))
    open(file_c, 'w').write(_file_c_tpl.format(file_h, matrix_defs))


if __name__ == "__main__":
    import sys
    _gen_tables(*sys.argv[1:3])
