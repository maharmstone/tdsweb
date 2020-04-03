cmake_minimum_required(VERSION 3.14.6)

find_package(nlohmann_json 3.2.0 REQUIRED)

project(tdsweb)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SRC_FILES src/tdsweb.cpp)

add_executable(tdsweb ${SRC_FILES})
set_property(TARGET tdsweb PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_link_libraries(tdsweb wscpp)
target_link_libraries(tdsweb tdscpp)

if(MSVC)
    target_compile_options(tdsweb PRIVATE /W4)
else()
    target_compile_options(tdsweb PRIVATE -Wall -Wextra -Wno-address-of-packed-member)
endif()

install(TARGETS tdsweb DESTINATION bin)
