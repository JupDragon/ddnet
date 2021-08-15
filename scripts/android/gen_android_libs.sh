CURDIR=$PWD
if [ -z ${1+x} ]; then 
	echo "Give a destination path where to run this script, please choose a path other than in the source directory"
	exit
fi

mkdir -p ${1}
cd ${1}

function build_cmake_lib() {
	if [ ! -d "${1}" ]; then
		git clone ${2} ${1}
	fi
	cd ${1}
	cp ${CURDIR}/scripts/android/cmake_lib_compile.sh cmake_lib_compile.sh
	./cmake_lib_compile.sh $_ANDROID_ABI_LEVEL
	cd ..
}

_ANDROID_ABI_LEVEL=24

if [ ! -d "android_libs" ]; then
	mkdir android_libs
fi
cd android_libs

build_cmake_lib curl https://github.com/curl/curl
build_cmake_lib freetype2 https://gitlab.freedesktop.org/freetype/freetype
build_cmake_lib sdl https://github.com/libsdl-org/SDL
build_cmake_lib ogg https://github.com/xiph/ogg
build_cmake_lib opus https://github.com/xiph/opus

_WAS_THERE_OPUSFILE=1
if [ ! -d "opusfile" ]; then
	git clone https://github.com/xiph/opusfile opusfile
	_WAS_THERE_OPUSFILE=0
fi
cd opusfile
if [[ $_WAS_THERE_OPUSFILE == 0 ]]; then
	./autogen.sh
fi
cp ${CURDIR}/scripts/android/make_android_opusfile.sh make_android_opusfile.sh
./make_android_opusfile.sh $_ANDROID_ABI_LEVEL
cd ..

# SQLite, just download and built by hand
if [ ! -d "sqlite3" ]; then
	wget https://www.sqlite.org/2021/sqlite-amalgamation-3360000.zip
	7z e sqlite-amalgamation-3360000.zip -osqlite3
fi

cd sqlite3
cp ${CURDIR}/scripts/android/make_android_sqlite3.sh make_android_sqlite3.sh
./make_android_sqlite3.sh $_ANDROID_ABI_LEVEL
cd ..

cd ..

mkdir ddnet-libs
function _copy_curl() {
	mkdir -p ddnet-libs/curl/android/lib$2
	cp android_libs/curl/build_android_$1/lib/libcurl.a ddnet-libs/curl/android/lib$2/libcurl.a
}

_copy_curl arm arm
_copy_curl arm64 arm64
_copy_curl x86 32
_copy_curl x86_64 64

mkdir ddnet-libs
function _copy_freetype2() {
	mkdir -p ddnet-libs/freetype/android/lib$2
	cp android_libs/freetype2/build_android_$1/libfreetype.a ddnet-libs/freetype/android/lib$2/libfreetype.a
}

_copy_freetype2 arm arm
_copy_freetype2 arm64 arm64
_copy_freetype2 x86 32
_copy_freetype2 x86_64 64

mkdir ddnet-libs
function _copy_sdl() {
	mkdir -p ddnet-libs/sdl/android/lib$2
	cp android_libs/sdl/build_android_$1/libSDL2.a ddnet-libs/sdl/android/lib$2/libSDL2.a
	cp android_libs/sdl/build_android_$1/libSDL2main.a ddnet-libs/sdl/android/lib$2/libSDL2main.a
	if [ ! -d "ddnet-libs/sdl/include/android" ]; then
		mkdir -p ddnet-libs/sdl/include/android
	fi
	cp -R android_libs/sdl/include/* ddnet-libs/sdl/include/android
}

_copy_sdl arm arm
_copy_sdl arm64 arm64
_copy_sdl x86 32
_copy_sdl x86_64 64

# copy java code from SDL2
rm -R ddnet-libs/sdl/java
mkdir -p ddnet-libs/sdl/java
cp -R android_libs/sdl/android-project/app/src/main/java/org ddnet-libs/sdl/java/

mkdir ddnet-libs
function _copy_ogg() {
	mkdir -p ddnet-libs/ogg/android/lib$2
	cp android_libs/ogg/build_android_$1/libogg.a ddnet-libs/opus/android/lib$2/libogg.a
}

_copy_ogg arm arm
_copy_ogg arm64 arm64
_copy_ogg x86 32
_copy_ogg x86_64 64

mkdir ddnet-libs
function _copy_opus() {
	mkdir -p ddnet-libs/opus/android/lib$2
	cp android_libs/opus/build_android_$1/libopus.a ddnet-libs/opus/android/lib$2/libopus.a
}

_copy_opus arm arm
_copy_opus arm64 arm64
_copy_opus x86 32
_copy_opus x86_64 64

mkdir ddnet-libs
function _copy_opusfile() {
	mkdir -p ddnet-libs/opus/android/lib$2
	cp android_libs/opusfile/build_$1/libopusfile.a ddnet-libs/opus/android/lib$2/libopusfile.a
}

_copy_opusfile arm arm
_copy_opusfile arm64 arm64
_copy_opusfile x86 32
_copy_opusfile x86_64 64

mkdir ddnet-libs
function _copy_sqlite3() {
	mkdir -p ddnet-libs/sqlite3/android/lib$2
	cp android_libs/sqlite3/build_$1/sqlite3.a ddnet-libs/sqlite3/android/lib$2/libsqlite3.a
}

_copy_sqlite3 arm arm
_copy_sqlite3 arm64 arm64
_copy_sqlite3 x86 32
_copy_sqlite3 x86_64 64
