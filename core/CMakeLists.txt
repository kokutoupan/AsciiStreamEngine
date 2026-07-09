add_library(astream_core STATIC
  src/dummy.cpp
)

if(WIN32)
  target_link_libraries(astream_core
    PUBLIC
      glm::glm
      ${LIBS}
      ZLIB::ZLIB
    PRIVATE winmm)
else()
  target_link_libraries(astream_core
    PUBLIC
      glm::glm
      ${LIBS}
      ZLIB::ZLIB)
endif()
target_include_directories(astream_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/util/include>
)
