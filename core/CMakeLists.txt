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

add_library(astream_core STATIC
  src/dummy.cpp
)

if(WIN32)
  target_link_libraries(astream_core
    PUBLIC
      glm::glm
      ${LIBS}
      ZLIB::ZLIB
      argon2
    PRIVATE winmm )
else()
  target_link_libraries(astream_core
    PUBLIC
      glm::glm
      ${LIBS}
      ZLIB::ZLIB
      argon2 )
endif()
target_include_directories(astream_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/util/include>
)
