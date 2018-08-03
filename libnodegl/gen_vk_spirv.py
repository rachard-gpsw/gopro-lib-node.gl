#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
# Copyright 2018 GoPro Inc.
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

import sys
import subprocess


def spirv_compile(dst_path, src_path):
    return subprocess.call(['glslangValidator', '-V', '-o', dst_path, src_path])


def c_str(bytecode):
    lines = []
    line = ''
    for i, byte in enumerate(bytecode):
        if line and i % (4*5) == 0:
            lines.append(line)
            line = ''
        line += '\\x%02X' % ord(byte)
    if line:
        lines.append(line)
    return '\\\n' + '\n'.join('    "%s"' % line for line in lines)


def gen(output):
    dst_frag, src_frag = 'vk/default-frag.spv', 'vk/default.frag'
    dst_vert, src_vert = 'vk/default-vert.spv', 'vk/default.vert'
    spirv_compile(dst_frag, src_frag)
    spirv_compile(dst_vert, src_vert)
    frag = open(dst_frag, 'rb').read()
    vert = open(dst_vert, 'rb').read()
    frag_str, vert_str = c_str(frag), c_str(vert)
    frag_size, vert_size = len(frag), len(vert)
    c_code = '''
const int ngli_vk_default_frag_size = %d;
const char *ngli_vk_default_frag = %s;

const int ngli_vk_default_vert_size = %d;
const char *ngli_vk_default_vert = %s;
''' % (frag_size, frag_str, vert_size, vert_str)
    open(output, 'w').write(c_code)


if __name__ == '__main__':
    if len(sys.argv) != 2:
        print('Usage: %s <output.c>' % sys.argv[0])
        sys.exit(0)
    gen(sys.argv[1])
