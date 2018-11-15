#
# Copyright 2011-2018 Branimir Karadzic. All rights reserved.
# License: https://github.com/bkaradzic/bnet#license-bsd-2-clause
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
	$(GENIE) --gcc=android-arm     gmake
	$(GENIE) --gcc=android-x86     gmake
	$(GENIE) --gcc=asmjs           gmake
	$(GENIE) --gcc=ios-arm         gmake
	$(GENIE) --gcc=ios-arm64       gmake
	$(GENIE) --gcc=ios-simulator   gmake
	$(GENIE) --gcc=ios-simulator64 gmake
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

.build/projects/gmake-osx:
	$(GENIE) --gcc=osx gmake
osx-debug32: .build/projects/gmake-osx
	make -C .build/projects/gmake-osx config=debug32
osx-release32: .build/projects/gmake-osx
	make -C .build/projects/gmake-osx config=release32
osx-debug64: .build/projects/gmake-osx
	make -C .build/projects/gmake-osx config=debug64
osx-release64: .build/projects/gmake-osx
	make -C .build/projects/gmake-osx config=release64
osx: osx-debug32 osx-release32 osx-debug64 osx-release64

.build/projects/gmake-ios-arm:
	$(GENIE) --gcc=ios-arm gmake
ios-arm-debug: .build/projects/gmake-ios-arm
	make -R -C .build/projects/gmake-ios-arm config=debug
ios-arm-release: .build/projects/gmake-ios-arm
	make -R -C .build/projects/gmake-ios-arm config=release
ios-arm: ios-arm-debug ios-arm-release

.build/projects/gmake-ios-simulator:
	$(GENIE) --gcc=ios-simulator gmake
ios-simulator-debug: .build/projects/gmake-ios-simulator
	make -R -C .build/projects/gmake-ios-simulator config=debug
ios-simulator-release: .build/projects/gmake-ios-simulator
	make -R -C .build/projects/gmake-ios-simulator config=release
ios-simulator: ios-simulator-debug ios-simulator-release

build-darwin: osx

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
