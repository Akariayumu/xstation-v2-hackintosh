# Makefile — builds FocalTechSPI.kext with Apple clang + MacKernelSDK.
# SPDX-License-Identifier: BSD-3-Clause

KEXT      := FocalTechSPI
BUNDLE_ID := io.xstation.driver.FocalTechSPI
VERSION   := 0.1.0
SDK       ?= MacKernelSDK
MIN_OS    ?= 15.5
BUILD     := build
CONFIG    := $(if $(DEBUG),Debug,Release)
OUT       := $(BUILD)/$(CONFIG)/$(KEXT).kext
OBJ       := $(BUILD)/$(CONFIG)/obj

CC        := xcrun clang
CXX       := xcrun clang++
SDKROOT   := $(shell xcrun --show-sdk-path 2>/dev/null)
RESDIR    := $(shell xcrun clang -print-resource-dir 2>/dev/null)
KMODCXX   := $(if $(wildcard $(SDKROOT)/usr/lib/libkmodc++.a),-lkmodc++,)
CCKEXT    := $(firstword $(wildcard $(RESDIR)/lib/darwin/libclang_rt.cc_kext.a) -lcc_kext)
ARCH      := -arch x86_64 -mmacosx-version-min=$(MIN_OS)

COMMON    := $(ARCH) -mkernel -nostdinc -fno-builtin -fno-common \
             -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT \
             -I$(SDK)/Headers -Wall -Wextra -Werror -Wno-unused-parameter \
             $(if $(DEBUG),-O0 -g -DDEBUG,-Os)
CXXFLAGS  := $(COMMON) -std=c++14 -fapple-kext -fno-rtti -fno-exceptions
CFLAGS    := $(COMMON) -std=gnu11
LDFLAGS   := $(ARCH) -mkernel -nostdlib -Xlinker -kext -Xlinker -export_dynamic \
             $(SDK)/Library/x86_64/libkmod.a $(KMODCXX) $(CCKEXT)

OBJS      := $(OBJ)/FocalTechSPI.o $(OBJ)/FocalTechSPIUserClient.o $(OBJ)/kmod_info.o

.PHONY: all clean
all: $(OUT)

$(OBJ):
	mkdir -p $@

$(OBJ)/FocalTechSPI.o: FocalTechSPI.cpp FocalTechSPI.hpp FocalTechSPIUserClient.hpp FocalTechSPIPure.hpp FocalTechSPIProtocol.h | $(OBJ)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ)/FocalTechSPIUserClient.o: FocalTechSPIUserClient.cpp FocalTechSPIUserClient.hpp FocalTechSPI.hpp FocalTechSPIProtocol.h | $(OBJ)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ)/kmod_info.c: | $(OBJ)
	printf '%s\n' \
	  '#include <mach/kmod.h>' \
	  'extern kern_return_t _start(kmod_info_t *, void *);' \
	  'extern kern_return_t _stop(kmod_info_t *, void *);' \
	  'KMOD_EXPLICIT_DECL($(BUNDLE_ID), "$(VERSION)", _start, _stop)' \
	  '__private_extern__ kmod_start_func_t *_realmain = 0;' \
	  '__private_extern__ kmod_stop_func_t *_antimain = 0;' > $@

$(OBJ)/kmod_info.o: $(OBJ)/kmod_info.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OUT): $(OBJS) Info.plist
	rm -rf $@ && mkdir -p $@/Contents/MacOS
	$(CXX) $(OBJS) $(LDFLAGS) -o $@/Contents/MacOS/$(KEXT)
	$(if $(DEBUG),,strip -x $@/Contents/MacOS/$(KEXT))
	cp Info.plist $@/Contents/Info.plist
	@echo "Built $@ ($(VERSION))"

clean:
	rm -rf $(BUILD)
