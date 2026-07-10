include(FetchContent)
FetchContent_Declare(
    argon2_official
    GIT_REPOSITORY https://github.com/P-H-C/phc-winner-argon2.git
    GIT_TAG        20190702
)
FetchContent_MakeAvailable(argon2_official)

add_library(astream_core STATIC
  src/dummy.cpp
)

if(WIN32)
  target_link_libraries(astream_core
    PUBLIC
      glm::glm
      ${LIBS}
      ZLIB::ZLIB
    PRIVATE winmm argon2)
else()
  target_link_libraries(astream_core
    PUBLIC
      glm::glm
      ${LIBS}
      ZLIB::ZLIB
    PRIVATE argon2)
endif()
target_include_directories(astream_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/util/include>
)
