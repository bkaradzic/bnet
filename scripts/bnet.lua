project "bnet"
	uuid "e72d44a0-ab28-11e0-9f1c-0800200c9a66"
	kind "StaticLib"

	includedirs {
		path.join(BNET_DIR, "include"),
	}

	files {
		path.join(BNET_DIR, "include/**.h"),
		path.join(BNET_DIR, "src/**.cpp"),
		path.join(BNET_DIR, "src/**.h"),
	}

	using_bx()

	configuration { "x32", "vs*" }
		includedirs { path.join(BNET_DIR, "3rdparty/openssl/lib/win32_", _ACTION, "include") }

	configuration { "x64", "vs*" }
		includedirs { path.join(BNET_DIR, "3rdparty/openssl/lib/win64_", _ACTION, "include") }

	configuration { "android-arm7" }
		includedirs { path.join(BNET_DIR, "3rdparty/openssl/lib/android_arm7/include") }

	configuration { "default-linux", "x32" }
		includedirs { path.join(BNET_DIR, "3rdparty/openssl/lib/linux-generic32/include") }

	configuration { "default-linux", "x64" }
		includedirs { path.join(BNET_DIR, "3rdparty/openssl/lib/linux-generic64/include") }

	configuration {}

	copyLib()
