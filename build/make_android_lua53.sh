if [ -z "$ANDROID_NDK" ]; then
    export ANDROID_NDK=~/android-ndk-r10e
fi

echo make armeabi-v7a ================================================
# linux
# STRIP_PATH=$ANDROID_NDK/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64/bin/arm-linux-androideabi-strip

# mac
STRIP_PATH=$ANDROID_NDK/toolchains/arm-linux-androideabi-4.9/prebuilt/darwin-x86_64/bin/arm-linux-androideabi-strip

mkdir -p build_v7a && cd build_v7a
cmake -DANDROID_ABI=armeabi-v7a -DCMAKE_TOOLCHAIN_FILE=../cmake/android.toolchain.cmake -DANDROID_TOOLCHAIN_NAME=arm-linux-androideabi-clang3.6 -DANDROID_NATIVE_API_LEVEL=android-9 -DANDROID_STRIP_EXEC=$STRIP_PATH ../
cd ..
cmake --build build_v7a --config Release
mkdir -p plugin_lua53/Plugins/Android/libs/armeabi-v7a/
cp build_v7a/libxlua.so plugin_lua53/Plugins/Android/libs/armeabi-v7a/libxlua.so

echo make x86 ================================================
# linux
# STRIP_PATH=$ANDROID_NDK/toolchains/x86-4.9/prebuilt/linux-x86_64/bin/i686-linux-android-strip

# mac
STRIP_PATH=$ANDROID_NDK/toolchains/x86-4.9/prebuilt/darwin-x86_64/bin/i686-linux-android-strip

mkdir -p build_x86 && cd build_x86
cmake -DANDROID_ABI=x86 -DCMAKE_TOOLCHAIN_FILE=../cmake/android.toolchain.cmake -DANDROID_TOOLCHAIN_NAME=x86-clang3.5 -DANDROID_NATIVE_API_LEVEL=android-9 -DANDROID_STRIP_EXEC=$STRIP_PATH ../
cd ..
cmake --build build_x86 --config Release
mkdir -p plugin_lua53/Plugins/Android/libs/x86/
cp build_x86/libxlua.so plugin_lua53/Plugins/Android/libs/x86/libxlua.so


