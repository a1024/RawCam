# Sets the minimum version of CMake required to build your native library.
# This ensures that a certain set of CMake features is available to
# your build.

cmake_minimum_required(VERSION 3.4.1)

# build cpufeatures as a static lib
add_library(cpufeatures STATIC
            ${ANDROID_NDK}/sources/android/cpufeatures/cpu-features.c)

# build app's shared lib

# set up neon build flag for file using intrinsics
# name: huff.cpp (It is named EXACTLY as this on disk, just like a normal source file)
# then set up neon flag for neon files
# [This example only build for armeabi-v7a, x86 could be done the same way]
if(${ANDROID_ABI} STREQUAL "armeabi-v7a")
  # make a list of neon files and add neon compiling flags to them
  set(neon_SRCS huff.cpp)

  set_property(SOURCE ${neon_SRCS}
               APPEND_STRING PROPERTY COMPILE_FLAGS " -mfpu=neon")
  add_definitions("-DHAVE_NEON=1")
elseif(${ANDROID_ABI} STREQUAL "arm64-v8a")
  set(neon_SRCS huff.cpp)

  add_definitions("-DHAVE_NEON=1")
else()
  set(neon_SRCS huff.cpp)
endif()

# Specifies a library name, specifies whether the library is STATIC or
# SHARED, and provides relative paths to the source code. You can
# define multiple libraries by adding multiple add_library() commands,
# and CMake builds them for you. When you build your app, Gradle
# automatically packages shared libraries with your APK.

add_library( # Specifies the name of the library.
             archiver

             # Sets the library as a shared library.
             SHARED

             # Provides a relative path to your source file(s).
             archiver.cpp
			${neon_SRCS})

# Searches for a specified prebuilt library and stores the path as a
# variable. Because CMake includes system libraries in the search path by
# default, you only need to specify the name of the public NDK library
# you want to add. CMake verifies that the library exists before
# completing its build.
find_library( # Sets the name of the path variable.
        log-lib

        # Specifies the name of the NDK library that
        # you want CMake to locate.
        log)

target_include_directories(archiver PRIVATE
    ${ANDROID_NDK}/sources/android/cpufeatures)

# Specifies libraries CMake should link to your target library. You
# can link multiple libraries, such as libraries you define in this
# build script, prebuilt third-party libraries, or system libraries.

target_link_libraries( # Specifies the target library.
        archiver

		android cpufeatures

        # Links the target library to the log library
        # included in the NDK.
        ${log-lib})