cmake_minimum_required(VERSION 3.12)
project(AsciiStreamEngine)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(ZLIB  REQUIRED)

# 数学ライブラリのリンクが必要な場合があるため
if(UNIX)
  set(LIBS m)
endif()

include_directories(core/include shaders)

add_subdirectory(core)
add_subdirectory(client)
add_subdirectory(examples)
