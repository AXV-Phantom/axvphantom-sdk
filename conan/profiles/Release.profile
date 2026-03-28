[settings]
os=Linux
arch=x86_64
compiler=gcc
compiler.version=15
compiler.libcxx=libstdc++11
compiler.cppstd=gnu17
build_type=Release

[options]
ffmpeg/*:shared=True
ffmpeg/*:gpl=False
