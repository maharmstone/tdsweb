cmake_minimum_required(VERSION 3.10)

project(tdsweb)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(nlohmann_json 3.2.0 REQUIRED)
find_package(Threads)

set(SRC_FILES src/tdsweb.cpp)

add_executable(tdsweb ${SRC_FILES})
set_property(TARGET tdsweb PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_link_libraries(tdsweb wscpp)
target_link_libraries(tdsweb tdscpp)
target_link_libraries(tdsweb Threads::Threads)

if(MSVC)
    target_compile_options(tdsweb PRIVATE /W4)
else()
    target_compile_options(tdsweb PRIVATE -Wall -Wextra -Wno-address-of-packed-member)
endif()

install(TARGETS tdsweb DESTINATION bin)
