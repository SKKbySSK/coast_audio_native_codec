# The Flutter tooling requires that developers have CMake 3.10 or later
# installed. You should not increase this version, as doing so will cause
# the plugin to fail to compile for some customers of the plugin.
cmake_minimum_required(VERSION 3.10)

project(coast_audio_native_codec_library VERSION 0.0.1 LANGUAGES C)

add_library(coast_audio_native_codec SHARED
  "coast_audio_native_codec.c"
)

set_target_properties(coast_audio_native_codec PROPERTIES
  PUBLIC_HEADER coast_audio_native_codec.h
  OUTPUT_NAME "coast_audio_native_codec"
)

target_compile_definitions(coast_audio_native_codec PUBLIC DART_SHARED_LIB)
