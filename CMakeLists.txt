cmake_minimum_required(VERSION 3.16)
project(bank_manager LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(bank_manager src/main.cpp)

if(WIN32)
    target_link_libraries(bank_manager PRIVATE ws2_32)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(bank_manager PRIVATE Threads::Threads)
endif()

file(COPY public DESTINATION ${CMAKE_BINARY_DIR})

add_custom_command(
    TARGET bank_manager POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/public"
            "${CMAKE_BINARY_DIR}/public"
    VERBATIM
)

if(MSVC)
    target_compile_options(bank_manager PRIVATE /W4 /permissive-)
else()
    target_compile_options(bank_manager PRIVATE -Wall -Wextra -Wpedantic)
endif()
