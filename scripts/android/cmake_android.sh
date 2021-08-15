export ANDROID_HOME=~/Android/Sdk
export MAKEFLAGS=-j32

export ANDROID_NDK_VERSION=$(ls $ANDROID_HOME/ndk | sort -n | tail -1)
ANDROID_NDK=$ANDROID_HOME/ndk/$ANDROID_NDK_VERSION

_DEFAULT_ANDROID_BUILD=x86
_DEFAULT_GAME_NAME=DDNet
_DEFAULT_BUILD_TYPE=Debug
_ANDROID_API_LEVEL=android-24

_ANDROID_SUB_BUILD_DIR=build_arch

_SHOW_USAGE_INFO=0

if [ -z ${1+x} ]; then
    printf "\e[31mDid not pass android build type, using default: ${_DEFAULT_ANDROID_BUILD}\e[30m\n"
	_SHOW_USAGE_INFO=1
else
	_DEFAULT_ANDROID_BUILD=$1
fi

if [ -z ${2+x} ]; then
    printf "\e[31mDid not pass game name, using default: ${_DEFAULT_GAME_NAME}\e[30m\n"
	_SHOW_USAGE_INFO=1
else
	_DEFAULT_GAME_NAME=$2
fi

if [ -z ${3+x} ]; then
    printf "\e[31mDid not pass build type, using default: ${_DEFAULT_BUILD_TYPE}\e[30m\n"
	_SHOW_USAGE_INFO=1
else
	_DEFAULT_BUILD_TYPE=$3
fi

_ANDROID_JAR_KEY_NAME=~/.android/debug.keystore
_ANDROID_JAR_KEY_PW=android
_ANDROID_JAR_KEY_ALIAS=androiddebugkey

if [ -z ${TW_KEY_NAME+x} ]; then
    printf "\e[31mDid not pass a key for the jar signer, using default: ${_ANDROID_JAR_KEY_NAME}\e[30m\n"
else
	_ANDROID_JAR_KEY_NAME=$TW_KEY_NAME
fi
if [ -z ${TW_KEY_PW+x} ]; then
    printf "\e[31mDid not pass a key pw for the jar signer, using default: ${_ANDROID_JAR_KEY_PW}\e[30m\n"
else
	_ANDROID_JAR_KEY_PW=$TW_KEY_PW
fi
if [ -z ${TW_KEY_ALIAS+x} ]; then
    printf "\e[31mDid not pass a key alias for the jar signer, using default: ${_ANDROID_JAR_KEY_ALIAS}\e[30m\n"
else
	_ANDROID_JAR_KEY_ALIAS=$TW_KEY_ALIAS
fi

export TW_KEY_NAME="${_ANDROID_JAR_KEY_NAME}"
export TW_KEY_PW=$_ANDROID_JAR_KEY_PW
export TW_KEY_ALIAS=$_ANDROID_JAR_KEY_ALIAS

_ANDROID_VERSION_CODE=1
if [ -z ${TW_VERSION_CODE+x} ]; then
    printf "\e[31mDid not pass a version code, using default: ${_ANDROID_VERSION_CODE}\e[30m\n"
else
	_ANDROID_VERSION_CODE=$TW_VERSION_CODE
fi

export TW_VERSION_CODE=$_ANDROID_VERSION_CODE

_ANDROID_VERSION_NAME="1.0"
if [ -z ${TW_VERSION_NAME+x} ]; then
    printf "\e[31mDid not pass a version name, using default: ${_ANDROID_VERSION_NAME}\e[30m\n"
else
	_ANDROID_VERSION_NAME=$TW_VERSION_NAME
fi

export TW_VERSION_NAME=$_ANDROID_VERSION_NAME

printf "\e[31mBuilding with setting, for arch: ${_DEFAULT_ANDROID_BUILD}, with build type: ${_DEFAULT_BUILD_TYPE}, with name: ${_DEFAULT_GAME_NAME}\e[1m\n"

if [ $_SHOW_USAGE_INFO == 1 ]; then	
    printf "\e[31mUsage: ./cmake_android.sh <x86/x86_64/arm/arm64/all> <Game name> <Debug/Release>\e[1m\n"
fi

function build_for_type() {
	cmake -H. -G "Ninja" -DPREFER_BUNDLED_LIBS=ON -DCMAKE_BUILD_TYPE=${_DEFAULT_BUILD_TYPE} -DANDROID_NATIVE_API_LEVEL=$_ANDROID_API_LEVEL -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake -DANDROID_NDK=$ANDROID_NDK -DANDROID_ABI="${2}" -DANDROID_ARM_NEON=TRUE -Bbuild_android/$_ANDROID_SUB_BUILD_DIR/$1 -DSERVER=OFF -DTOOLS=OFF -DDEV=TRUE -DCMAKE_CROSSCOMPILING=ON -DPREFER_BUNDLED_LIBS=ON
	cd build_android/$_ANDROID_SUB_BUILD_DIR/$1
	cmake --build . --target DDNet

	cd ../../..
}

mkdir build_android

if [[ "${_DEFAULT_ANDROID_BUILD}" == "arm" || "${_DEFAULT_ANDROID_BUILD}" == "all" ]]; then
	build_for_type arm armeabi-v7a arm eabi &
fi

if [[ "${_DEFAULT_ANDROID_BUILD}" == "arm64" || "${_DEFAULT_ANDROID_BUILD}" == "all" ]]; then
	build_for_type arm64 arm64-v8a aarch64 &
fi

if [[ "${_DEFAULT_ANDROID_BUILD}" == "x86" || "${_DEFAULT_ANDROID_BUILD}" == "all" ]]; then
	build_for_type x86 x86 i686 &
fi

if [[ "${_DEFAULT_ANDROID_BUILD}" == "x86_64" || "${_DEFAULT_ANDROID_BUILD}" == "x64" || "${_DEFAULT_ANDROID_BUILD}" == "all" ]]; then
	build_for_type x86_64 x86_64 x86_64 &
fi

wait

cd build_android

mkdir -p src/main

function copy_dummy_files() {
	rm ./$2
	cp ../$1 $2
}

function copy_dummy_files_rec() {
	rm -R ./$2/$1
	cp -R ../$1 $2
}

copy_dummy_files scripts/android/files/build.sh build.sh
copy_dummy_files scripts/android/files/gradle-wrapper.jar gradle-wrapper.jar
copy_dummy_files scripts/android/files/build.gradle build.gradle
copy_dummy_files scripts/android/files/gradle-wrapper.properties gradle-wrapper.properties
copy_dummy_files scripts/android/files/gradle.properties gradle.properties
copy_dummy_files scripts/android/files/local.properties local.properties
copy_dummy_files scripts/android/files/proguard-rules.pro proguard-rules.pro
copy_dummy_files scripts/android/files/settings.gradle settings.gradle
copy_dummy_files scripts/android/files/AndroidManifest.xml src/main/AndroidManifest.xml
copy_dummy_files_rec scripts/android/files/res src/main

function copy_libs() {
	mkdir -p lib/$2
	cp $_ANDROID_SUB_BUILD_DIR/$1/libDDNet.so lib/$2
	cp $_ANDROID_SUB_BUILD_DIR/$1/libs/libSDL2.so lib/$2
	cp $_ANDROID_SUB_BUILD_DIR/$1/libs/libhidapi.so lib/$2
}

if [[ "${_DEFAULT_ANDROID_BUILD}" == "arm" || "${_DEFAULT_ANDROID_BUILD}" == "all" ]]; then
	copy_libs arm armeabi-v7a arm eabi
fi

if [[ "${_DEFAULT_ANDROID_BUILD}" == "arm64" || "${_DEFAULT_ANDROID_BUILD}" == "all" ]]; then
	copy_libs arm64 arm64-v8a aarch64
fi

if [[ "${_DEFAULT_ANDROID_BUILD}" == "x86" || "${_DEFAULT_ANDROID_BUILD}" == "all" ]]; then
	copy_libs x86 x86 i686
fi

if [[ "${_DEFAULT_ANDROID_BUILD}" == "x86_64" || "${_DEFAULT_ANDROID_BUILD}" == "x64" || "${_DEFAULT_ANDROID_BUILD}" == "all" ]]; then
	copy_libs x86_64 x86_64 x86_64
fi

_DEFAULT_ANDROID_BUILD_DUMMY=$_DEFAULT_ANDROID_BUILD
if [[ "${_DEFAULT_ANDROID_BUILD}" == "all" ]]; then
	_DEFAULT_ANDROID_BUILD_DUMMY=arm
fi

mkdir assets
cp -R $_ANDROID_SUB_BUILD_DIR/$_DEFAULT_ANDROID_BUILD_DUMMY/data ./assets/

# create integrity file for extracting assets
cd assets

tmpfile=$(mktemp /tmp/hash_strings.XXX)

find data -iname "*" -type f -print0 | while IFS= read -r -d $'\0' file; do
	sha_hash=$(sha256sum "$file" | cut -d' ' -f 1)
	echo "$file $sha_hash" >> "$tmpfile"
done

full_hash=$(sha256sum "$tmpfile" | cut -d' ' -f 1)

rm "integrity.txt"
echo $full_hash > "integrity.txt"
cat $tmpfile >> "integrity.txt"

cd ..

echo "Building..."

rm -R src/main/java/tw
mkdir -p src/main/java/tw/DDNet
cp ../scripts/android/files/java/tw/DDNet/NativeMain.java src/main/java/tw/DDNet/NativeMain.java

rm -R src/main/java/org
cp -R ../scripts/android/files/java/org src/main/java/
cp -R ../ddnet-libs/sdl/java/org src/main/java/

source ./build.sh $ANDROID_HOME $_DEFAULT_GAME_NAME $_DEFAULT_BUILD_TYPE

cd ..
