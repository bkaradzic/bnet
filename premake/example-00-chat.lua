project "example-00-chat"
	uuid "1544c710-ad76-11e0-9f1c-0800200c9a66"
	kind "ConsoleApp"

	debugdir (BNET_DIR .. "examples/runtime/")

	includedirs {
		BX_DIR .. "include",
		BNET_DIR .. "include",
	}

	files {
		BNET_DIR .. "examples/common/**",
		BNET_DIR .. "examples/00-chat/chat.cpp"
	}

	links {
		"bnet",
	}

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
