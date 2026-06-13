add_executable(demo_cube_multiplayer cube_multiplayer/main.cpp)
target_link_libraries(demo_cube_multiplayer PRIVATE ascii3d_core ${LIBS} ZLIB::ZLIB)

# レンダリングのみ
add_executable(render_only renderer_sample.cpp)
target_link_libraries(render_only PRIVATE ascii3d_core ${LIBS})

