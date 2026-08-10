project "bnet"
	uuid "e72d44a0-ab28-11e0-9f1c-0800200c9a66"
	kind "StaticLib"

	includedirs {
		path.join(BNET_DIR, "include"),
		path.join(BNET_DIR, "3rdparty/mbedtls/include"),
		path.join(BNET_DIR, "3rdparty/mbedtls/library"),
	}

	files {
		path.join(BNET_DIR, "include/**.h"),
		path.join(BNET_DIR, "src/**.cpp"),
		path.join(BNET_DIR, "src/**.h"),
		path.join(BNET_DIR, "3rdparty/mbedtls/library/**.c"),
	}

	using_bx()

	configuration {}

	copyLib()
