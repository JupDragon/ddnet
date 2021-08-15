ANDROID_HOME=~/Android/Sdk
ANDROID_NDK=$ANDROID_HOME/ndk/$(ls $ANDROID_HOME/ndk | sort -n | tail -1)
echo $ANDROID_NDK

export MAKEFLAGS=-j32

function compile_source() {
	cmake -H. -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DANDROID_NATIVE_API_LEVEL=android-$1 -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI="${3}" -DANDROID_ARM_NEON=TRUE -B$2 -DBUILD_SHARED_LIBS=OFF -DHIDAPI_SKIP_LIBUSB=TRUE -DCMAKE_USE_OPENSSL=OFF -DHIDAPI=OFF -DOP_DISABLE_HTTP=ON -DOP_DISABLE_EXAMPLES=ON -DOP_DISABLE_DOCS=ON
	cd $2
	cmake --build .
	cd ..
}

compile_source $1 build_android_arm armeabi-v7a &
compile_source $1 build_android_arm64 arm64-v8a &
compile_source $1 build_android_x86 x86 &
compile_source $1 build_android_x86_64 x86_64 &

wait
