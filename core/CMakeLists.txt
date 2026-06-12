add_library(ascii3d_core STATIC
  src/dummy.cpp
)

target_include_directories(ascii3d_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
