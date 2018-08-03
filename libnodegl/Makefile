#
# Copyright 2016 GoPro Inc.
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

include ../common.mak

DEBUG_GL ?= no
LOGTRACE ?= no
WGET     ?= wget

ifeq ($(DEBUG_GL),yes)
	PROJECT_CFLAGS += -DDEBUG_GL
endif

ifeq ($(LOGTRACE),yes)
	PROJECT_CFLAGS += -DLOGTRACE
endif

ifeq ($(DEBUG_MEM),yes)
	PROJECT_CFLAGS += -DDEBUG_MEM
endif

LD_SYM_FILE   = $(LIB_BASENAME).symexport
LD_SYM_OPTION = --version-script
LD_SYM_DATA   = "{\n\tglobal: ngl_*;\n\tlocal: *;\n};\n"
DYLIBSUFFIX = so
ifeq ($(TARGET_OS),$(filter $(TARGET_OS),Darwin iPhone))
	DYLIBSUFFIX = dylib
	LD_SYM_OPTION = -exported_symbols_list
	LD_SYM_DATA   = "_ngl_*\n"
else
ifeq ($(TARGET_OS),MinGW-w64)
	DYLIBSUFFIX = dll
endif # MinGW
endif # Darwin/iPhone

ifeq ($(SHARED),yes)
	LIBSUFFIX = $(DYLIBSUFFIX)
else
	LIBSUFFIX = a
endif
LIB_BASENAME = libnodegl
LIB_NAME     = $(LIB_BASENAME).$(LIBSUFFIX)
LIB_PCNAME   = $(LIB_BASENAME).pc

LIB_OBJS = animation.o              \
           api.o                    \
           bstr.o                   \
           darray.o                 \
           deserialize.o            \
           dot.o                    \
           drawutils.o              \
           format.o                 \
           graphicconfig.o          \
           hmap.o                   \
           hwupload.o               \
           hwupload_common.o        \
           image.o                  \
           log.o                    \
           math_utils.o             \
           memory.o                 \
           node_animatedbuffer.o    \
           node_animation.o         \
           node_animkeyframe.o      \
           node_block.o             \
           node_buffer.o            \
           node_camera.o            \
           node_circle.o            \
           node_compute.o           \
           node_computeprogram.o    \
           node_geometry.o          \
           node_graphicconfig.o     \
           node_group.o             \
           node_hud.o               \
           node_identity.o          \
           node_media.o             \
           node_program.o           \
           node_quad.o              \
           node_render.o            \
           node_rotate.o            \
           node_rtt.o               \
           node_scale.o             \
           node_text.o              \
           node_texture.o           \
           node_timerangefilter.o   \
           node_timerangemodes.o    \
           node_transform.o         \
           node_translate.o         \
           node_triangle.o          \
           node_uniform.o           \
           node_userswitch.o        \
           nodes.o                  \
           params.o                 \
           serialize.o              \
           spirv.o                  \
           transforms.o             \
           utils.o                  \

LIB_OBJS_ARCH_aarch64 = asm_aarch64.o

LIB_OBJS += $(LIB_OBJS_ARCH_$(ARCH))

ifeq ($(BACKEND),vk)
LIB_OBJS += backend_vk.o            \
            buffer_vk.o             \
            glstate_vk.o            \
            pipeline_vk.o           \
            texture_vk.o            \
            topology_vk.o           \

else
LIB_OBJS += backend_gl.o            \
            buffer.o                \
            fbo.o                   \
            glcontext.o             \
            glstate.o               \
            hwconv.o                \
            pipeline.o              \
            program.o               \
            texture.o               \
            topology.o              \

LIB_EXTRA_OBJS_Linux     = glcontext_egl.o
LIB_EXTRA_OBJS_Darwin    = glcontext_nsgl.o hwupload_videotoolbox_darwin.o
LIB_EXTRA_OBJS_Android   = glcontext_egl.o hwupload_mediacodec.o
LIB_EXTRA_OBJS_iPhone    = glcontext_eagl.o hwupload_videotoolbox_ios.o
LIB_EXTRA_OBJS_MinGW-w64 = glcontext_wgl.o
endif
LIB_EXTRA_OBJS_Android += jni_utils.o android_utils.o android_looper.o android_surface.o android_handler.o android_handlerthread.o

# XXX/HACK
VKSDK_VER = 1.1.77.0
VKSDK_FILE = vulkansdk-macos-$(VKSDK_VER)
VKSDK_TGZ = $(VKSDK_FILE).tar.gz
VKSDK_BASEURL = https://sdk.lunarg.com/sdk/download/$(VKSDK_VER)/mac/
MVK_ROOT = vulkansdk-macos-$(VKSDK_VER)/MoltenVK/

LIB_CFLAGS                 = -fPIC
ifneq ($(BACKEND),vk)
LIB_EXTRA_CFLAGS_Linux     = -DHAVE_GLPLATFORM_EGL
LIB_EXTRA_CFLAGS_Darwin    = -DHAVE_GLPLATFORM_NSGL
LIB_EXTRA_CFLAGS_Android   = -DHAVE_GLPLATFORM_EGL
LIB_EXTRA_CFLAGS_iPhone    = -DHAVE_GLPLATFORM_EAGL
LIB_EXTRA_CFLAGS_MinGW-w64 = -DHAVE_GLPLATFORM_WGL
else
LIB_EXTRA_CFLAGS_Darwin    = -I$(MVK_ROOT)/include/
LIB_EXTRA_CFLAGS_iPhone    = -I$(MVK_ROOT)/include/
endif

LIB_LDLIBS                 = -lm -lpthread
LIB_EXTRA_LDLIBS_Linux     =
ifneq ($(BACKEND),vk)
LIB_EXTRA_LDLIBS_Darwin    = -framework OpenGL -framework CoreVideo -framework CoreFoundation -framework AppKit -framework IOSurface
LIB_EXTRA_LDLIBS_iPhone    = -framework CoreMedia
else
LIB_EXTRA_LDLIBS_Darwin    = -L$(MVK_ROOT)/macOS -Wl,-rpath,$(MVK_ROOT)/macOS -lMoltenVK
LIB_EXTRA_LDLIBS_iPhone    = -L$(MVK_ROOT)/iOS -Wl,-rpath,$(MVK_ROOT)/iOS -lMoltenVK
endif
LIB_EXTRA_LDLIBS_Android   = -legl
LIB_EXTRA_LDLIBS_MinGW-w64 = -lopengl32 -lgdi32

LIB_PKG_CONFIG_LIBS               = "libsxplayer >= 9.3.1"
ifeq ($(BACKEND),vk)
LIB_EXTRA_PKG_CONFIG_LIBS_Linux   = x11 vulkan
else
LIB_EXTRA_PKG_CONFIG_LIBS_Linux   = x11 gl egl
endif
LIB_EXTRA_PKG_CONFIG_LIBS_Darwin  =
LIB_EXTRA_PKG_CONFIG_LIBS_Android = libavcodec
LIB_EXTRA_PKG_CONFIG_LIBS_iPhone  =

VAAPI_PKG_CONFIG_LIBS = "libva-x11 >= 1.1.0" "libva-drm >= 1.1.0"

ifeq ($(TARGET_OS),Linux)
	ENABLE_VAAPI_X11 ?= $(shell $(PKG_CONFIG) --exists $(VAAPI_PKG_CONFIG_LIBS) && echo yes || echo no)
else
	ENABLE_VAAPI_X11 = no
endif

ifneq ($(BACKEND),vk)
ifeq ($(ENABLE_VAAPI_X11),yes)
	PROJECT_CFLAGS += -DHAVE_VAAPI_X11
	LIB_EXTRA_OBJS_Linux += hwupload_vaapi.o vaapi.o
	LIB_PKG_CONFIG_LIBS += $(VAAPI_PKG_CONFIG_LIBS)
endif
endif

LIB_OBJS   += $(LIB_EXTRA_OBJS_$(TARGET_OS))
LIB_CFLAGS += $(LIB_EXTRA_CFLAGS_$(TARGET_OS))
LIB_LDLIBS += $(LIB_EXTRA_LDLIBS_$(TARGET_OS))
LIB_CFLAGS += $(shell $(PKG_CONFIG) --cflags $(LIB_PKG_CONFIG_LIBS) $(LIB_EXTRA_PKG_CONFIG_LIBS_$(TARGET_OS)))
LIB_LDLIBS += $(shell $(PKG_CONFIG) --libs   $(LIB_PKG_CONFIG_LIBS) $(LIB_EXTRA_PKG_CONFIG_LIBS_$(TARGET_OS)))

LIB_DEPS = $(LIB_OBJS:.o=.d)


all: $(LIB_PCNAME) $(LIB_NAME)


#
# Library
#
$(LIB_NAME): CFLAGS  = $(PROJECT_CFLAGS) $(LIB_CFLAGS)
$(LIB_NAME): LDLIBS  = $(PROJECT_LDLIBS) $(LIB_LDLIBS)
$(LIB_NAME): LDFLAGS = -Wl,$(LD_SYM_OPTION),$(LD_SYM_FILE)
$(LIB_NAME): CPPFLAGS += -MMD -MP
$(LIB_NAME): $(LD_SYM_FILE) $(LIB_OBJS)
ifeq ($(SHARED),yes)
	$(CC) $(LDFLAGS) $(LIB_OBJS) -shared -o $@ $(LDLIBS)
else
	$(AR) rcs $@ $(LIB_OBJS)
endif


#
# Symbols
#
$(LD_SYM_FILE):
	$(shell printf $(LD_SYM_DATA) > $(LD_SYM_FILE))


#
# pkg-config
#
define headver
$(shell sed -nE 's/^\#define NODEGL_VERSION_$(1)[^0-9]*([0-9]*)/\1/p' nodegl.h)
endef

$(LIB_PCNAME): VERSION = $(call headver,MAJOR).$(call headver,MINOR).$(call headver,MICRO)
$(LIB_PCNAME): LDLIBS = $(PROJECT_LDLIBS) $(LIB_LDLIBS)
ifeq ($(SHARED),yes)
$(LIB_PCNAME): DEP_PRIVATE_LIBS = $(LDLIBS)
else
$(LIB_PCNAME): DEP_LIBS = $(LDLIBS)
endif
$(LIB_PCNAME): $(LIB_PCNAME).tpl
	sed -e "s#PREFIX#$(PREFIX)#" \
	    -e "s#DEP_LIBS#$(DEP_LIBS)#" \
	    -e "s#DEP_PRIVATE_LIBS#$(DEP_PRIVATE_LIBS)#" \
	    -e "s#VERSION#$(VERSION)#" \
	    $^ > $@


#
# Specifications
#
SPECS_FILE = nodes.specs
gen_specs$(EXESUF): CFLAGS = $(PROJECT_CFLAGS) $(LIB_CFLAGS)
gen_specs$(EXESUF): LDLIBS = $(PROJECT_LDLIBS) $(LIB_LDLIBS)
gen_specs$(EXESUF): gen_specs.o $(LIB_OBJS)
gen_specs$(EXESUF):
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

updatespecs: gen_specs$(EXESUF)
	./gen_specs$(EXESUF) > $(SPECS_FILE)


#
# Doc generation
#
gen_doc$(EXESUF): CFLAGS = $(PROJECT_CFLAGS) $(LIB_CFLAGS)
gen_doc$(EXESUF): LDLIBS = $(PROJECT_LDLIBS) $(LIB_LDLIBS)
gen_doc$(EXESUF): gen_doc.o $(LIB_OBJS)
gen_doc$(EXESUF):
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

updatedoc: gen_doc$(EXESUF)
	./gen_doc$(EXESUF) > doc/libnodegl.md


#
# OpenGL function wrappers
#
gen-gl-wrappers: gl.xml
	$(PYTHON) gen-gl-wrappers.py $^
gl.xml:
	$(WGET) https://raw.githubusercontent.com/KhronosGroup/OpenGL-Registry/master/xml/gl.xml


#
# Vulkan default shader SPIR-V generation
#
vk_default_vert.h: vk/default.vert
	glslangValidator -V --vn vk_default_vert -o $@ $<
vk_default_frag.h: vk/default.frag
	glslangValidator -V --vn vk_default_frag -o $@ $<
node_program.o: vk_default_vert.h vk_default_frag.h

#
# Tests
#
TESTS = asm             \
        darray          \
        draw            \
        hmap            \
        spirv           \
        utils           \

TESTPROGS = $(addprefix test_,$(TESTS))
$(TESTPROGS): CFLAGS = $(PROJECT_CFLAGS) $(LIB_CFLAGS)
$(TESTPROGS): LDLIBS = $(PROJECT_LDLIBS) $(LIB_LDLIBS)

testprogs: $(TESTPROGS)

test_asm: LDLIBS = $(PROJECT_LDLIBS) -lm
test_asm: test_asm.o math_utils.o $(LIB_OBJS_ARCH_$(ARCH))
test_darray: test_darray.o darray.o memory.o
test_draw: test_draw.o drawutils.o
test_hmap: test_hmap.o utils.o memory.o
test_spirv: test_spirv.o spirv.o hmap.o utils.o darray.o memory.o
test_utils: test_utils.o utils.o memory.o


#
# Misc/general
#
clean:
	$(RM) $(LIB_BASENAME).so $(LIB_BASENAME).dylib $(LIB_BASENAME).a $(LIB_BASENAME).dll
	$(RM) $(LIB_OBJS) $(LIB_DEPS)
	$(RM) gen_specs.o gen_specs$(EXESUF)
	$(RM) gen_doc.o gen_doc$(EXESUF)
	$(RM) $(LIB_PCNAME)
	$(RM) $(LD_SYM_FILE)
	$(RM) $(TESTPROGS)
	$(RM) $(addsuffix .o,$(TESTPROGS))

install: $(LIB_NAME) $(LIB_PCNAME)
	install -d $(DESTDIR)$(PREFIX)/lib
	install -d $(DESTDIR)$(PREFIX)/lib/pkgconfig
	install -d $(DESTDIR)$(PREFIX)/include
	install -d $(DESTDIR)$(PREFIX)/share
	install -d $(DESTDIR)$(PREFIX)/share/nodegl
	install -d $(DESTDIR)$(PREFIX)/share/nodegl/java
	install -d $(DESTDIR)$(PREFIX)/share/nodegl/java/org
	install -d $(DESTDIR)$(PREFIX)/share/nodegl/java/org/nodegl
ifeq ($(TARGET_OS),MinGW-w64)
ifeq ($(SHARED),yes)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 644 $(LIB_NAME) $(DESTDIR)$(PREFIX)/bin
endif # shared
endif # mingw-w64
	install -m 644 $(LIB_NAME) $(DESTDIR)$(PREFIX)/lib
	install -m 644 $(LIB_PCNAME) $(DESTDIR)$(PREFIX)/lib/pkgconfig
	install -m 644 nodegl.h $(DESTDIR)$(PREFIX)/include/nodegl.h
	install -m 644 $(SPECS_FILE) $(DESTDIR)$(PREFIX)/share/nodegl
	install -m 644 android/java/OnFrameAvailableListener.java $(DESTDIR)$(PREFIX)/share/nodegl/java/org/nodegl

uninstall:
	$(RM) $(DESTDIR)$(PREFIX)/lib/$(LIB_NAME)
	$(RM) $(DESTDIR)$(PREFIX)/bin/$(LIB_NAME)
	$(RM) $(DESTDIR)$(PREFIX)/lib/pkgconfig/$(LIB_PCNAME)
	$(RM) $(DESTDIR)$(PREFIX)/include/nodegl.h
	$(RM) -r $(DESTDIR)$(PREFIX)/share/nodegl

# XXX/HACK
vksdk: $(VKSDK_TGZ)
	tar xf $<
$(VKSDK_TGZ):
	wget $(VKSDK_BASEURL)/$@

.PHONY: all updatespecs clean install uninstall gen-gl-wrappers vksdk update_vk_spirv

-include $(LIB_DEPS)
