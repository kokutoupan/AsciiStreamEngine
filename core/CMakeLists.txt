add_library(astream_core STATIC
  src/dummy.cpp
)

target_include_directories(astream_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
