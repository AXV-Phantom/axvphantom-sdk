from pathlib import Path

from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain, cmake_layout


ROOT_DIR = Path(__file__).resolve().parent
PACKAGE_VERSION = (ROOT_DIR / "VERSION").read_text(encoding="utf-8").strip()


class AxvPhantomConan(ConanFile):
    name = "axvphantom"
    version = PACKAGE_VERSION
    package_type = "library"
    license = "LGPL-3.0-or-later"
    settings = "os", "arch", "compiler", "build_type"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
    }
    generators = ()
    no_copy_source = True
    exports_sources = (
        "CMakeLists.txt",
        "CMakePresets.json",
        "cmake/*",
        "conan/profiles/*",
        "include/*",
        "schema/*",
        "src/*",
        "tests/*",
    )

    default_options = {
        "shared": True,
        "fPIC": True,
        "ffmpeg/*:shared": True,
        "ffmpeg/*:gpl": False,
        "ffmpeg/*:with_lzma": False,
        "ffmpeg/*:with_openjpeg": False,
    }
    default_options.update(
        {
            "opencv/*:highgui": False,
            "opencv/*:with_gtk": False,
            "opencv/*:with_qt": False,
            "opencv/*:with_wayland": False,
            "ffmpeg/*:avdevice": True,
            "ffmpeg/*:avcodec": True,
            "ffmpeg/*:avformat": True,
            "ffmpeg/*:swresample": True,
            "ffmpeg/*:swscale": True,
            "ffmpeg/*:postproc": True,
            "ffmpeg/*:avfilter": True,
            "ffmpeg/*:with_asm": True,
            "ffmpeg/*:with_zlib": True,
            "ffmpeg/*:with_bzip2": True,
            "ffmpeg/*:with_libiconv": False,
            "ffmpeg/*:with_freetype": False,
            "ffmpeg/*:with_libxml2": False,
            "ffmpeg/*:with_fontconfig": False,
            "ffmpeg/*:with_fribidi": False,
            "ffmpeg/*:with_harfbuzz": False,
            "ffmpeg/*:with_libjxl": False,
            "ffmpeg/*:with_openapv": False,
            "ffmpeg/*:with_openh264": False,
            "ffmpeg/*:with_opus": False,
            "ffmpeg/*:with_vorbis": False,
            "ffmpeg/*:with_zeromq": False,
            "ffmpeg/*:with_sdl": False,
            "ffmpeg/*:with_libx264": False,
            "ffmpeg/*:with_libx265": False,
            "ffmpeg/*:with_libvpx": False,
            "ffmpeg/*:with_libmp3lame": False,
            "ffmpeg/*:with_libfdk_aac": False,
            "ffmpeg/*:with_libwebp": False,
            "ffmpeg/*:with_libalsa": False,
            "ffmpeg/*:with_pulse": False,
            "ffmpeg/*:with_vaapi": False,
            "ffmpeg/*:with_vdpau": False,
            "ffmpeg/*:with_vulkan": False,
            "ffmpeg/*:with_whisper": False,
            "ffmpeg/*:with_xcb": False,
            "ffmpeg/*:with_soxr": False,
            "ffmpeg/*:with_appkit": False,
            "ffmpeg/*:with_avfoundation": False,
            "ffmpeg/*:with_coreimage": False,
            "ffmpeg/*:with_audiotoolbox": False,
            "ffmpeg/*:with_videotoolbox": False,
            "ffmpeg/*:with_programs": False,
            "ffmpeg/*:with_libsvtav1": False,
            "ffmpeg/*:with_libaom": False,
            "ffmpeg/*:with_libdav1d": False,
            "ffmpeg/*:with_libdrm": False,
            "ffmpeg/*:with_jni": False,
            "ffmpeg/*:with_mediacodec": False,
            "ffmpeg/*:with_xlib": False,
            "vulkan-loader/*:with_wsi_xcb": False,
            "vulkan-loader/*:with_wsi_xlib": False,
            "vulkan-loader/*:with_wsi_wayland": False,
        }
    )

    def requirements(self):
        self.requires("opencv/[>=4 <5]")
        self.requires("flatbuffers/[>=23 <24]")
        self.requires("ffmpeg/[>=6 <7]")
        self.requires("liburing/[>=2 <3]")
        self.requires("vulkan-loader/[>=1 <2]")
        self.requires("openssl/[>=3 <4]")

    def layout(self):
        cmake_layout(self)

    def generate(self):
        toolchain = CMakeToolchain(self)
        toolchain.generator = "Ninja"
        toolchain.variables["CMAKE_CXX_STANDARD"] = 23
        toolchain.variables["CMAKE_CXX_STANDARD_REQUIRED"] = True
        toolchain.variables["CMAKE_CXX_EXTENSIONS"] = False
        toolchain.variables["AXVP_BUILD_TESTS"] = True
        toolchain.variables["AXVP_BUILD_BENCH"] = True
        toolchain.variables["AXVP_EDGEARM"] = str(self.settings.arch) == "armv8"
        toolchain.generate()

        deps = CMakeDeps(self)
        deps.generate()
