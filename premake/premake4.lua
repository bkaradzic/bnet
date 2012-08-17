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

BNET_DIR = (path.getabsolute("..") .. "/")
local BNET_BUILD_DIR = (BNET_DIR .. ".build/")
local BNET_THIRD_PARTY_DIR = (BNET_DIR .. "3rdparty/")

local XEDK = os.getenv("XEDK")
if not XEDK then XEDK = "<you must install XBOX SDK>" end

location (BNET_BUILD_DIR .. "projects/" .. _ACTION)

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
	BNET_DIR .. "../bx/include",
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
	targetdir (BNET_BUILD_DIR .. "win32_" .. _ACTION .. "/bin")
	objdir (BNET_BUILD_DIR .. "win32_" .. _ACTION .. "/obj")
	includedirs {
		BNET_THIRD_PARTY_DIR .. "msinttypes",
		BNET_THIRD_PARTY_DIR .. "openssl/lib/win32_" .. _ACTION .. "/include",
	}
	libdirs { BNET_THIRD_PARTY_DIR .. "openssl/lib/win32_" .. _ACTION .. "/lib" }

configuration { "x64", "vs*" }
	defines { "WIN32" }
	targetdir (BNET_BUILD_DIR .. "win64_" .. _ACTION .. "/bin")
	objdir (BNET_BUILD_DIR .. "win64_" .. _ACTION .. "/obj")
	includedirs {
		BNET_THIRD_PARTY_DIR .. "msinttypes",
		BNET_THIRD_PARTY_DIR .. "openssl/lib/win64_" .. _ACTION .. "/include",
	}
	libdirs { BNET_THIRD_PARTY_DIR .. "openssl/lib/win64_" .. _ACTION .. "/lib" }

configuration { "x32", "mingw" }
	defines { "WIN32" }
	targetdir (BNET_BUILD_DIR .. "win32_mingw" .. "/bin")
	objdir (BNET_BUILD_DIR .. "win32_mingw" .. "/obj")
	includedirs { BNET_THIRD_PARTY_DIR .. "openssl/lib/win32_mingw/include" }
	libdirs { BNET_THIRD_PARTY_DIR .. "openssl/lib/win32_mingw/lib" }

configuration { "x64", "mingw" }
	defines { "WIN32" }
	targetdir (BNET_BUILD_DIR .. "win64_mingw" .. "/bin")
	objdir (BNET_BUILD_DIR .. "win64_mingw" .. "/obj")
	includedirs { BNET_THIRD_PARTY_DIR .. "openssl/lib/win64_mingw/include" }
	libdirs { BNET_THIRD_PARTY_DIR .. "openssl/lib/win64_mingw/lib" }

configuration { "x32", "nacl" }
	targetdir (BNET_BUILD_DIR .. "nacl32" .. "/bin")
	objdir (BNET_BUILD_DIR .. "nacl32" .. "/obj")

configuration { "x64", "nacl" }
	targetdir (BNET_BUILD_DIR .. "nacl64" .. "/bin")
	objdir (BNET_BUILD_DIR .. "nacl64" .. "/obj")

configuration { "x32", "linux" }
	targetdir (BNET_BUILD_DIR .. "linux32" .. "/bin")
	objdir (BNET_BUILD_DIR .. "linux32" .. "/obj")

configuration { "x64", "linux" }
	targetdir (BNET_BUILD_DIR .. "linux64" .. "/bin")
	objdir (BNET_BUILD_DIR .. "linux64" .. "/obj")

configuration { "Xbox360" }
	defines { "_XBOX", "NOMINMAX" }
	targetdir (BNET_BUILD_DIR .. "xbox360" .. "/bin")
	objdir (BNET_BUILD_DIR .. "xbox360" .. "/obj")
	includedirs { BNET_THIRD_PARTY_DIR .. "openssl/lib/xbox360/include" }
	libdirs { BNET_THIRD_PARTY_DIR .. "openssl/lib/xbox360/lib" }

configuration {} -- reset configuration

dofile "bnet.lua"
dofile "chat.lua"
dofile "http.lua"
