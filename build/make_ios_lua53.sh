mkdir -p build_ios && cd build_ios
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=../cmake/ios.toolchain.cmake -DPLATFORM=OS64 -GXcode ../
cd ..
cmake --build build_ios --config Release
mkdir -p plugin_lua53/Plugins/iOS/
cp build_ios/Release-iphoneos/libxlua.a plugin_lua53/Plugins/iOS/libxlua.a
cp build_ios/zlib/Release-iphoneos/libz.a plugin_lua53/Plugins/iOS/libz.a

# pitfall: libcares.a require libresolve.9.tbd, just add it to Framework
cp build_ios/c-ares/Release-iphoneos/libcares.a plugin_lua53/Plugins/iOS/libcares.a

