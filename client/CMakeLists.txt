add_executable(ascii_client
    client.cpp
    NetworkStreamClient.cpp
    TerminalController.cpp
)
target_link_libraries(ascii_client PRIVATE astream_core ${LIBS} ZLIB::ZLIB)

if(UNIX)
    add_executable(ascii_emulator
        emulator_client.cpp
        NetworkStreamClient.cpp
        TerminalController.cpp
    )
    target_link_libraries(ascii_emulator PRIVATE astream_core ${LIBS} ZLIB::ZLIB)
endif()
