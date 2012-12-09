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

BNET_DIR = (path.getabsolute("..") .. "/")
local BNET_BUILD_DIR = (BNET_DIR .. ".build/")
local BNET_THIRD_PARTY_DIR = (BNET_DIR .. "3rdparty/")
BX_DIR = (BNET_DIR .. "../bx/")

dofile (BX_DIR .. "premake/toolchain.lua")
toolchain(BNET_BUILD_DIR, BNET_THIRD_PARTY_DIR)

function copyLib()
end

dofile "bnet.lua"
dofile "example-00-chat.lua"
dofile "example-01-http.lua"
