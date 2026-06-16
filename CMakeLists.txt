cmake_minimum_required(VERSION 4.0)
project(AsciiStreamEngine)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

find_package(ZLIB  REQUIRED)

# ==========================================
# 1. GLMの追加 (FetchContent)
# ==========================================
include(FetchContent)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG        1.0.3 # 安定したコミットハッシュ（または1.0.1などのタグ）
)

# GLM内部のテストビルドなどを無効化してビルドを高速化
set(GLM_TEST_ENABLE OFF CACHE INTERNAL "")

# GLMをダウンロードして CMake のターゲットとして利用可能にする
FetchContent_MakeAvailable(glm)

# 数学ライブラリのリンクが必要な場合があるため
if(UNIX)
  set(LIBS m)
endif()

include_directories(core/include shaders)

add_subdirectory(core)
add_subdirectory(client)
add_subdirectory(examples)
