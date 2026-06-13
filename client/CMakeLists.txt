add_executable(ascii_client client.cpp)
target_link_libraries(ascii_client PRIVATE ascii3d_core ${LIBS} ZLIB::ZLIB)
