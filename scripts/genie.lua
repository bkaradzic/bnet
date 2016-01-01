--
-- Copyright 2010-2016 Branimir Karadzic. All rights reserved.
-- License: https://github.com/bkaradzic/bnet#license-bsd-2-clause
--

solution "bnet"
	configurations {
		"Debug",
		"Release",
	}

	platforms {
		"x32",
		"x64",
		"Xbox360",
		"Native", -- for targets where bitness is not specified
	}

	language "C++"

BNET_DIR = (path.getabsolute("..") .. "/")
local BNET_BUILD_DIR = (BNET_DIR .. ".build/")
local BNET_THIRD_PARTY_DIR = (BNET_DIR .. "3rdparty/")
BX_DIR = (BNET_DIR .. "../bx/")

defines {
	"BX_CONFIG_ENABLE_MSVC_LEVEL4_WARNINGS=1"
}

dofile (BX_DIR .. "scripts/toolchain.lua")
toolchain(BNET_BUILD_DIR, BNET_THIRD_PARTY_DIR)

function copyLib()
end

function exampleProject(_name, _uuid)

	project ("example-" .. _name)
		uuid (_uuid)
		kind "ConsoleApp"

	configuration {}

	debugdir (BNET_DIR .. "examples/runtime/")

	includedirs {
		BX_DIR .. "include",
		BNET_DIR .. "include",
	}

	files {
		BNET_DIR .. "examples/" .. _name .. "/**.cpp",
		BNET_DIR .. "examples/" .. _name .. "/**.h",
	}

	links {
		"bnet",
		"example-common",
	}

	configuration { "vs*" }

	configuration { "x32", "vs*" }
		libdirs { BNET_DIR .. "3rdparty/openssl/lib/win32_" .. _ACTION .. "/lib" }

	configuration { "x64", "vs*" }
		libdirs { BNET_DIR .. "3rdparty/openssl/lib/win64_" .. _ACTION .. "/lib" }

	configuration { "x32 or x64", "windows" }
		links {
			"libeay32",
			"ssleay32",
			"ws2_32",
		}

	configuration { "android*" }
		kind "ConsoleApp"
		targetextension ".so"
		linkoptions {
			"-shared",
		}

	configuration { "nacl or nacl-arm" }
		kind "ConsoleApp"
		targetextension ".nexe"
		links {
			"ppapi",
			"pthread",
		}

	configuration { "pnacl" }
		kind "ConsoleApp"
		targetextension ".pexe"
		links {
			"ppapi",
			"pthread",
		}

	configuration { "osx" }
		linkoptions {
			"-framework Cocoa",
		}

	configuration { "ios*" }
		kind "ConsoleApp"
		linkoptions {
			"-framework CoreFoundation",
			"-framework Foundation",
			"-framework UIKit",
			"-framework QuartzCore",
		}

	configuration {}

	strip()
end

dofile "bnet.lua"
dofile "example-common.lua"
exampleProject("00-chat", "1544c710-ad76-11e0-9f1c-0800200c9a66")
exampleProject("01-http", "35161d20-ab2b-11e0-9f1c-0800200c9a66")
