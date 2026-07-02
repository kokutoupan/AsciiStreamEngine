cmake_minimum_required(VERSION 3.31)
project(AsciiStreamEngine)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

if(MSVC)
  # ソースコードの読み込み(source-set)も実行時(execution-set)もすべてUTF-8として処理させる
  add_compile_options(/utf-8)
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)

if(ipo_supported)
  message(STATUS "IPO/LTO is supported by the compiler. Enabling for Release builds.")
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
else()
  message(WARNING "IPO/LTO is not supported on this platform: ${ipo_error}")
endif()

find_package(ZLIB)

if(ZLIB_FOUND)
  message(STATUS "Found system ZLIB: ${ZLIB_LIBRARIES}")
  # システムの ZLIB が見つかった場合は、そのまま ZLIB::ZLIB ターゲットが利用可能
else()
  message(STATUS "System ZLIB not found. Downloading via FetchContent...")

  include(FetchContent)
  FetchContent_Declare(
        zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG        v1.3.1
    )
  set(ZLIB_BUILD_EXAMPLES OFF CACHE INTERNAL "")
  FetchContent_MakeAvailable(zlib)

  # FetchContent が生成した zlibstatic ターゲットを ZLIB::ZLIB としてエイリアス化
  if(NOT TARGET ZLIB::ZLIB)
    if(TARGET zlibstatic)
      add_library(ZLIB::ZLIB INTERFACE IMPORTED)
      target_link_libraries(ZLIB::ZLIB INTERFACE zlibstatic)
      target_include_directories(ZLIB::ZLIB INTERFACE ${zlib_SOURCE_DIR} ${zlib_BINARY_DIR})
    endif()
  endif()
endif()

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
elseif(WIN32)
  set(LIBS ws2_32) # 先述のWindows用ソケット
endif()


# =============================================================================
# エンジン補助機能: OBJアセットをC++ソースにコンパイルするマクロ
# =============================================================================
function(astream_compile_assets TARGET_NAME)
  set(OBJ_FILES ${ARGN})
  set(GENERATED_HEADERS "")

  foreach(OBJ_PATH ${OBJ_FILES})
    get_filename_component(OBJ_NAME ${OBJ_PATH} NAME_WE)

    set(OUTPUT_HPP "${CMAKE_CURRENT_BINARY_DIR}/generated_assets/${OBJ_NAME}.hpp")

    add_custom_command(
        OUTPUT ${OUTPUT_HPP}
        COMMAND deno run --allow-read --allow-write ${CMAKE_SOURCE_DIR}/tools/obj_to_cpp.ts ${CMAKE_CURRENT_SOURCE_DIR}/${OBJ_PATH} ${OUTPUT_HPP}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${OBJ_PATH} ${CMAKE_SOURCE_DIR}/tools/obj_to_cpp.ts
        COMMENT "AstreamEngine: Compiling asset ${OBJ_PATH} -> ${OUTPUT_HPP}"
    )
    list(APPEND GENERATED_HEADERS ${OUTPUT_HPP})
  endforeach()

  # ターゲットごとにユニークなカスタムターゲット（アセットジェネレータ）を作る
  set(ASSET_GEN_TARGET "${TARGET_NAME}_asset_gen")
  add_custom_target(${ASSET_GEN_TARGET} DEPENDS ${GENERATED_HEADERS})

  add_dependencies(${TARGET_NAME} ${ASSET_GEN_TARGET})

  # インクルードパスを通す
  target_include_directories(${TARGET_NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated_assets")
endfunction()

add_subdirectory(core)
add_subdirectory(client)
add_subdirectory(examples)
