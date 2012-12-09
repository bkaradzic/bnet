project "bnet"
	uuid "e72d44a0-ab28-11e0-9f1c-0800200c9a66"
	kind "StaticLib"

	includedirs {
		BX_DIR .. "include",
		BNET_DIR .. "include",
	}

	configuration { "x32", "vs*" }
		includedirs { BNET_DIR .. "3rdparty/openssl/lib/win32_" .. _ACTION .. "/include" }

	configuration { "x64", "vs*" }
		includedirs { BNET_DIR .. "3rdparty/openssl/lib/win64_" .. _ACTION .. "/include" }

	configuration { "android-arm7" }
		includedirs { BNET_DIR .. "3rdparty/openssl/lib/android_arm7/include" }

	configuration { "default-linux", "x32" }
		includedirs { BNET_DIR .. "3rdparty/openssl/lib/linux-generic32/include" }

	configuration { "default-linux", "x64" }
		includedirs { BNET_DIR .. "3rdparty/openssl/lib/linux-generic64/include" }

	configuration "Debug"
		defines {
			"BNET_CONFIG_DEBUG=1",
		}

	configuration {}

	files {
		BNET_DIR .. "include/**.h",
		BNET_DIR .. "src/**.cpp",
		BNET_DIR .. "src/**.h",
	}

	copyLib()
