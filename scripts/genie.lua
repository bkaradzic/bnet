--
-- Copyright 2010-2025 Branimir Karadzic. All rights reserved.
-- License: https://github.com/bkaradzic/bnet/blob/master/LICENSE
--

newoption {
	trigger = "with-openssl",
	description = "Enable OpenSSL integration.",
}

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
BX_DIR   = os.getenv("BX_DIR")

local BNET_BUILD_DIR = path.join(BNET_DIR, ".build")
local BNET_THIRD_PARTY_DIR = path.join(BNET_DIR, "3rdparty")
if not BX_DIR then
	BX_DIR = path.getabsolute(path.join(BNET_DIR, "../bx") )
end

defines {
	"BX_CONFIG_ENABLE_MSVC_LEVEL4_WARNINGS=1"
}

dofile (path.join(BX_DIR, "scripts/toolchain.lua") )
toolchain(BNET_BUILD_DIR, BNET_THIRD_PARTY_DIR)

function copyLib()
end

group "libs"
dofile(path.join(BX_DIR, "scripts/bx.lua"))
dofile "bnet.lua"

function exampleProject(_name)

	project ("example-" .. _name)
		uuid (os.uuid("example-" .. _name))
		kind "ConsoleApp"

	configuration {}

	debugdir (path.join(BNET_DIR, "examples/runtime") )

	includedirs {
		path.join(BNET_DIR, "include"),
	}

	files {
		path.join(BNET_DIR, "examples", _name, "**.cpp"),
		path.join(BNET_DIR, "examples", _name, "**.h"),
	}

	links {
		"bnet",
	}

	using_bx()

	configuration { "vs* or mingw*" }
		links {
			"psapi",
			"ws2_32",
		}

	if _OPTIONS["with-openssl"] then
		configuration { "x32", "vs*" }
			libdirs { path.join(BNET_DIR, "3rdparty/openssl/lib/win32_", _ACTION, "lib") }

		configuration { "x64", "vs*" }
			libdirs { path.join(BNET_DIR, "3rdparty/openssl/lib/win64_", _ACTION, "lib") }

		configuration { "vs* or mingw*" }
			links {
				"libeay32",
				"ssleay32",
			}
	end

	configuration { "android*" }
		kind "ConsoleApp"
		targetextension ".so"
		linkoptions {
			"-shared",
		}

	configuration { "osx*" }
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

group "examples"
exampleProject("00-chat")
exampleProject("01-http")
