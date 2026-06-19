add_executable(ascii_client client.cpp)
target_link_libraries(ascii_client PRIVATE astream_core ${LIBS} ZLIB::ZLIB)
