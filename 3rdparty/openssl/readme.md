# Building OpenSSL

Downloaded latest version of OpenSSL:
http://www.openssl.org/source/

The latest version at the moment of writing this is openssl-1.0.0d.

## MS Windows

Notice:
You'll have to manually copy lib.pdb into /lib folder to avoid linker warnings about missing debug information.

### Visual Studio 2008 32-bit

	vcvars x86
	perl Configure VC-WIN32 no-asm no-shared --prefix=/openssl/win32_vs2008
	ms\do_ms
	nmake -f ms\nt.mak install

### Visual Studio 2008 64-bit

	vcvars amd64
	perl Configure VC-WIN64A no-asm no-shared --prefix=/openssl/win64_vs2008
	ms\do_win64a
	nmake -f ms\nt.mak install

### MinGW 32-bit

	ms\mingw32.bat no-asm no-shared --prefix=/openssl/win32_mingw

## Linux

### Android-NDK

	See STANDALONE-TOOLCHAIN.HTML in Android-NDK http://developer.android.com/sdk/ndk/overview.html

	./Configure linux-generic32 no-idea no-bf no-cast no-seed no-md2 -DL_ENDIAN --prefix=/openssl/android_arm7
	make depend
	make install CC=<path to Android-NDK compiler> RANLIB=<...>

### Ubuntu-12.04

	i386 CC="gcc -m32" ./config --prefix=<path>/linux-generic32
	make install

	CC="gcc -m64" ./config --prefix=<path>/linux-generic64
	make install

	cp <path>/linux-generic32/lib/*.a <path to 3rdparty/openssl>/linux-generic32/lib/
	cp <path>/linux-generic32/include <path to 3rdparty/openssl>/linux-generic32/
	cp <path>/linux-generic64/lib/*.a <path to 3rdparty/openssl>/linux-generic64/lib/
	cp <path>/linux-generic64/include <path to 3rdparty/openssl>/linux-generic64/
