mkdir build32 & pushd build32
cmake -DLUAC_COMPAT_FORMAT=ON -DLUAC_COMPAT_32BIT=ON -A Win32 ..

popd
cmake --build build32 --config Release
pause