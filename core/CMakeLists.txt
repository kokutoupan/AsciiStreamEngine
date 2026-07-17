option(USE_PROFILER "Enable the loop profiler" OFF)

include(FetchContent)
FetchContent_Declare(
    argon2_official
    GIT_REPOSITORY https://github.com/P-H-C/phc-winner-argon2.git
    GIT_TAG        20190702
)
FetchContent_MakeAvailable(argon2_official)
if(NOT TARGET argon2)
  add_library(argon2 STATIC
        ${argon2_official_SOURCE_DIR}/src/argon2.c
        ${argon2_official_SOURCE_DIR}/src/core.c
        ${argon2_official_SOURCE_DIR}/src/blake2/blake2b.c
        ${argon2_official_SOURCE_DIR}/src/thread.c
        ${argon2_official_SOURCE_DIR}/src/encoding.c
        ${argon2_official_SOURCE_DIR}/src/ref.c # MSVC互換性の高いリファレンス実装を使用
    )

  # 外部(astream_coreなど)からもインクルードできるように PUBLIC でパスを通す
  target_include_directories(argon2 PUBLIC
        ${argon2_official_SOURCE_DIR}/include
    )

  # MSVC環境固有のC言語セキュア警告を抑制する（必要に応じて）
  if(MSVC)
    target_compile_definitions(argon2 PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()
endif()

add_library(astream_core INTERFACE)

if(USE_PROFILER)
  target_compile_definitions(astream_core INTERFACE ENABLE_PROFILER)
  message(STATUS "Loop Profiler: ENABLED")
else()
  message(STATUS "Loop Profiler: DISABLED")
endif()

if(WIN32)
  target_link_libraries(astream_core
    INTERFACE
      glm::glm
      ${LIBS}
      ZLIB::ZLIB
      argon2
      winmm )
else()
  target_link_libraries(astream_core
    INTERFACE
      glm::glm
      ${LIBS}
      ZLIB::ZLIB
      argon2 )
endif()

file(GLOB_RECURSE ASTREAM_HEADERS
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp"
)
target_sources(astream_core
    INTERFACE
        ${ASTREAM_HEADERS}
)
target_include_directories(astream_core INTERFACE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/util/include>
)
