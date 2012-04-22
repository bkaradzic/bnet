--
-- Copyright 2010-2011 Branimir Karadzic. All rights reserved.
-- License: http://www.opensource.org/licenses/BSD-2-Clause
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
	}

	language "C++"

newoption {
	trigger = "gcc",
	value = "GCC",
	description = "Choose GCC flavor",
	allowed = {
		{ "mingw", "MinGW" },
		{ "nacl", "Google Native Client" },
	}
}

-- Avoid error when invoking premake4 --help.
if (_ACTION == nil) then return end

ROOT_DIR = (path.getabsolute("..") .. "/")
BUILD_DIR = (ROOT_DIR .. ".build/")
THIRD_PARTY_DIR = (ROOT_DIR .. "3rdparty/")

local XEDK = os.getenv("XEDK")
if not XEDK then XEDK = "<you must install XBOX SDK>" end

location (BUILD_DIR .. "projects/" .. _ACTION)

if _ACTION == "gmake" then

	if "linux" ~= os.get() and nil == _OPTIONS["gcc"] then
		print("GCC flavor must be specified!")
		os.exit(1)
	end

	if "mingw" == _OPTIONS["gcc"] then
		premake.gcc.cc = "$(MINGW)/bin/mingw32-gcc"
		premake.gcc.cxx = "$(MINGW)/bin/mingw32-g++"
		premake.gcc.ar = "$(MINGW)/bin/ar"
	end

	if "nacl" == _OPTIONS["gcc"] then
		premake.gcc.cc = "$(NACL)/bin/nacl-gcc"
		premake.gcc.cxx = "$(NACL)/bin/nacl-g++"
		premake.gcc.ar = "$(NACL)/bin/nacl-ar"
	end
end

flags {
	"StaticRuntime",
	"NoMinimalRebuild",
	"NoPCH",
	"NativeWChar",
--	"ExtraWarnings",
	"NoRTTI",
	"NoExceptions",
	"Symbols",
}

includedirs {
	ROOT_DIR .. "../bx/include",
}

configuration "Debug"
	defines {
		"BNET_BUILD_DEBUG=1",
	}
	targetsuffix "Debug"

configuration "Release"
	defines {
		"BNET_BUILD_RELEASE=1",
	}
	targetsuffix "Release"

configuration { "mingw" }
	links {
		"ssl",
		"crypto",
		"gdi32",
	}

configuration { "vs*" }
	defines {
		"_HAS_EXCEPTIONS=0",
		"_HAS_ITERATOR_DEBUGGING=0",
		"_SCL_SECURE=0",
		"_CRT_SECURE_NO_WARNINGS",
		"_CRT_SECURE_NO_DEPRECATE",
		"__STDC_LIMIT_MACROS",
		"__STDC_FORMAT_MACROS",
		"__STDC_CONSTANT_MACROS",
	}
	links {
		"libeay32",
		"ssleay32",
	}

configuration { "x32", "vs*" }
	defines { "WIN32" }
	targetdir (BUILD_DIR .. "win32_" .. _ACTION .. "/bin")
	objdir (BUILD_DIR .. "win32_" .. _ACTION .. "/obj")
	includedirs {
		THIRD_PARTY_DIR .. "msinttypes",
		THIRD_PARTY_DIR .. "openssl/lib/win32_" .. _ACTION .. "/include",
	}
	libdirs { THIRD_PARTY_DIR .. "openssl/lib/win32_" .. _ACTION .. "/lib" }

configuration { "x64", "vs*" }
	defines { "WIN32" }
	targetdir (BUILD_DIR .. "win64_" .. _ACTION .. "/bin")
	objdir (BUILD_DIR .. "win64_" .. _ACTION .. "/obj")
	includedirs {
		THIRD_PARTY_DIR .. "msinttypes",
		THIRD_PARTY_DIR .. "openssl/lib/win64_" .. _ACTION .. "/include",
	}
	libdirs { THIRD_PARTY_DIR .. "openssl/lib/win64_" .. _ACTION .. "/lib" }

configuration { "x32", "mingw" }
	defines { "WIN32" }
	targetdir (BUILD_DIR .. "win32_mingw" .. "/bin")
	objdir (BUILD_DIR .. "win32_mingw" .. "/obj")
	includedirs { THIRD_PARTY_DIR .. "openssl/lib/win32_mingw/include" }
	libdirs { THIRD_PARTY_DIR .. "openssl/lib/win32_mingw/lib" }

configuration { "x64", "mingw" }
	defines { "WIN32" }
	targetdir (BUILD_DIR .. "win64_mingw" .. "/bin")
	objdir (BUILD_DIR .. "win64_mingw" .. "/obj")
	includedirs { THIRD_PARTY_DIR .. "openssl/lib/win64_mingw/include" }
	libdirs { THIRD_PARTY_DIR .. "openssl/lib/win64_mingw/lib" }

configuration { "x32", "nacl" }
	targetdir (BUILD_DIR .. "nacl32" .. "/bin")
	objdir (BUILD_DIR .. "nacl32" .. "/obj")

configuration { "x64", "nacl" }
	targetdir (BUILD_DIR .. "nacl64" .. "/bin")
	objdir (BUILD_DIR .. "nacl64" .. "/obj")

configuration { "x32", "linux" }
	targetdir (BUILD_DIR .. "linux32" .. "/bin")
	objdir (BUILD_DIR .. "linux32" .. "/obj")

configuration { "x64", "linux" }
	targetdir (BUILD_DIR .. "linux64" .. "/bin")
	objdir (BUILD_DIR .. "linux64" .. "/obj")

configuration { "Xbox360" }
	defines { "_XBOX", "NOMINMAX" }
	targetdir (BUILD_DIR .. "xbox360" .. "/bin")
	objdir (BUILD_DIR .. "xbox360" .. "/obj")
	includedirs { THIRD_PARTY_DIR .. "openssl/lib/xbox360/include" }
	libdirs { THIRD_PARTY_DIR .. "openssl/lib/xbox360/lib" }

configuration {} -- reset configuration

project "bnet"
	uuid "e72d44a0-ab28-11e0-9f1c-0800200c9a66"
	kind "StaticLib"

	includedirs {
		"../include",
	}

	files {
		"../include/**.h",
		"../src/**.cpp",
		"../src/**.h",
	}

project "http"
	uuid "35161d20-ab2b-11e0-9f1c-0800200c9a66"
	kind "ConsoleApp"

	includedirs {
		"../include",
	}

	files {
		"../examples/common/**",
		"../examples/http/http.cpp"
	}

	links {
		"bnet",
	}

	configuration { "mingw" }
		links {
			"ssl",
			"crypto",
			"gdi32",
		}

	configuration { "vs*" }
		links {
			"libeay32",
			"ssleay32",
		}

	configuration { "x32 or x64", "windows" }
		links {
			"ws2_32",
		}

	configuration { "Xbox360" }
		links {
			"xonline",
		}

	configuration {}

project "chat"
	uuid "1544c710-ad76-11e0-9f1c-0800200c9a66"
	kind "ConsoleApp"

	includedirs {
		"../include",
	}

	files {
		"../examples/common/**",
		"../examples/chat/chat.cpp"
	}

	links {
		"bnet",
	}

	configuration { "x32 or x64", "windows" }
		links {
			"ws2_32",
		}

	configuration { "Xbox360" }
		links {
			"xonline",
		}

