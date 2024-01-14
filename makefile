#
# Copyright 2011-2024 Branimir Karadzic. All rights reserved.
# License: https://github.com/bkaradzic/bnet/blob/master/LICENSE
#

UNAME := $(shell uname)
ifeq ($(UNAME),$(filter $(UNAME),Linux Darwin))
ifeq ($(UNAME),$(filter $(UNAME),Darwin))
OS=darwin
else
OS=linux
endif
else
OS=windows
endif

BX_DIR?=../bx
GENIE?=$(BX_DIR)/tools/bin/$(OS)/genie

all:
	$(GENIE)                       vs2017
	$(GENIE)                       vs2019
	$(GENIE) --gcc=android-arm     gmake
	$(GENIE) --gcc=android-x86     gmake
	$(GENIE) --gcc=asmjs           gmake
	$(GENIE) --gcc=osx-x64         gmake
	$(GENIE) --gcc=osx-arm64       gmake
	$(GENIE) --gcc=ios-arm         gmake
	$(GENIE) --gcc=ios-arm64       gmake
	$(GENIE) --gcc=rpi             gmake
	$(GENIE)                       xcode8

.build/projects/gmake-android-arm:
	$(GENIE) --gcc=android-arm gmake
android-arm-debug: .build/projects/gmake-android-arm
	make -R -C .build/projects/gmake-android-arm config=debug
android-arm-release: .build/projects/gmake-android-arm
	make -R -C .build/projects/gmake-android-arm config=release
android-arm: android-arm-debug android-arm-release

.build/projects/gmake-android-x86:
	$(GENIE) --gcc=android-x86 gmake
android-x86-debug: .build/projects/gmake-android-x86
	make -R -C .build/projects/gmake-android-x86 config=debug
android-x86-release: .build/projects/gmake-android-x86
	make -R -C .build/projects/gmake-android-x86 config=release
android-x86: android-x86-debug android-x86-release

.build/projects/gmake-linux:
	$(GENIE) --gcc=linux-gcc gmake
linux-debug32: .build/projects/gmake-linux
	make -R -C .build/projects/gmake-linux config=debug32
linux-release32: .build/projects/gmake-linux
	make -R -C .build/projects/gmake-linux config=release32
linux-debug64: .build/projects/gmake-linux
	make -R -C .build/projects/gmake-linux config=debug64
linux-release64: .build/projects/gmake-linux
	make -R -C .build/projects/gmake-linux config=release64
linux: linux-debug32 linux-release32 linux-debug64 linux-release64

.build/projects/gmake-mingw-gcc:
	$(GENIE) --gcc=mingw-gcc gmake
mingw-gcc-debug32: .build/projects/gmake-mingw-gcc
	make -R -C .build/projects/gmake-mingw-gcc config=debug32
mingw-gcc-release32: .build/projects/gmake-mingw-gcc
	make -R -C .build/projects/gmake-mingw-gcc config=release32
mingw-gcc-debug64: .build/projects/gmake-mingw-gcc
	make -R -C .build/projects/gmake-mingw-gcc config=debug64
mingw-gcc-release64: .build/projects/gmake-mingw-gcc
	make -R -C .build/projects/gmake-mingw-gcc config=release64
mingw-gcc: mingw-gcc-debug32 mingw-gcc-release32 mingw-gcc-debug64 mingw-gcc-release64

.build/projects/gmake-osx-x64:
	$(GENIE) --gcc=osx-x64 gmake
osx-x64-debug: .build/projects/gmake-osx-x64 ## Build - macOS x64 Debug
	$(MAKE) -C .build/projects/gmake-osx-x64 config=debug
osx-x64-release: .build/projects/gmake-osx-x64 ## Build - macOS x64 Release
	$(MAKE) -C .build/projects/gmake-osx-x64 config=release
osx-x64: osx-x64-debug osx-x64-release ## Build - macOS x64 Debug and Release

.build/projects/gmake-ios-arm:
	$(GENIE) --gcc=ios-arm gmake
ios-arm-debug: .build/projects/gmake-ios-arm
	make -R -C .build/projects/gmake-ios-arm config=debug
ios-arm-release: .build/projects/gmake-ios-arm
	make -R -C .build/projects/gmake-ios-arm config=release
ios-arm: ios-arm-debug ios-arm-release

build-darwin: osx-x64

build-linux: linux-debug64 linux-release64

build-windows: mingw

build: build-$(OS)

analyze:
	cppcheck src/
	cppcheck examples/

clean: ## Clean all intermediate files.
	@echo Cleaning...
	-@rm -rf .build
	@mkdir .build
