cmake_minimum_required(VERSION 4.0)
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
  # 引数以降に渡されたすべてのOBJファイルを取得
  set(OBJ_FILES ${ARGN})
  set(GENERATED_SOURCES "")

  foreach(OBJ_PATH ${OBJ_FILES})
    # ファイル名（拡張子なし）を取得 (例: Assets/cube.obj -> cube)
    get_filename_component(OBJ_NAME ${OBJ_PATH} NAME_WE)

    # 各ターゲットごとのビルドディレクトリ配下に.cppを出力
    set(OUTPUT_CPP "${CMAKE_CURRENT_BINARY_DIR}/generated_assets/${OBJ_NAME}.cpp")

    # カスタムコマンドの登録（スクリプトはルート直下の tools/ から呼び出す）
    add_custom_command(
            OUTPUT ${OUTPUT_CPP}
            COMMAND deno run --allow-read --allow-write ${CMAKE_SOURCE_DIR}/tools/obj_to_cpp.ts ${CMAKE_CURRENT_SOURCE_DIR}/${OBJ_PATH} ${OUTPUT_CPP}
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${OBJ_PATH} ${CMAKE_SOURCE_DIR}/tools/obj_to_cpp.ts
            COMMENT "AstreamEngine: Compiling asset ${OBJ_PATH} -> ${OUTPUT_CPP}"
        )
    list(APPEND GENERATED_SOURCES ${OUTPUT_CPP})
  endforeach()

  # 呼び出し元のターゲットに、自動生成された.cpp群を注入
  target_sources(${TARGET_NAME} PRIVATE ${GENERATED_SOURCES})

  # インクルードパスも自動で通す
  target_include_directories(${TARGET_NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated_assets")
endfunction()

add_subdirectory(core)
add_subdirectory(client)
add_subdirectory(examples)
