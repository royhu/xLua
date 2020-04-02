mkdir build64 & pushd build64
cmake -DLUAC_COMPAT_FORMAT=ON -DLUAC_COMPAT_32BIT=ON ..
popd
cmake --build build64 --config Release
pause