cmake_minimum_required(VERSION 3.15)
project(PainterTextureSetsCombiner)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# zlib — adjust paths to your local install
set(ZLIB_INCLUDE_DIR "D:/Lib/zlib/zlib-1.3.2")
set(ZLIB_LIBRARY "D:/Lib/zlib/zlib-1.3.2/build/libzd.dll.a")

# stb lib
set(stb_SOURCE_DIR "D:/Lib/stb")

find_package(ZLIB REQUIRED)

add_executable(combiner
    src/main.cpp
    src/png_decoder.cpp
    src/config_parser.cpp
    src/fnmatch.c
    src/safe-ctype.c
)

target_include_directories(combiner PRIVATE
    src
    ${stb_SOURCE_DIR}
    ${ZLIB_INCLUDE_DIRS}
)

target_link_libraries(combiner PRIVATE
    ${ZLIB_LIBRARIES}
)

target_compile_definitions(combiner PRIVATE
    _CRT_SECURE_NO_WARNINGS
)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
