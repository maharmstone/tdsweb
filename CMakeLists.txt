cmake_minimum_required(VERSION 3.10)

project(tdsweb)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(nlohmann_json 3.2.0 REQUIRED)
find_package(Threads REQUIRED)
find_package(tdscpp REQUIRED)
find_package(wscpp REQUIRED)
find_package(xlcpp REQUIRED)

set(SRC_FILES src/tdsweb.cpp
    src/win.cpp)

add_executable(tdsweb ${SRC_FILES})
set_property(TARGET tdsweb PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

target_link_libraries(tdsweb wscpp)
target_link_libraries(tdsweb tdscpp)
target_link_libraries(tdsweb xlcpp)
target_link_libraries(tdsweb Threads::Threads)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND WIN32)
    target_link_options(tdsweb PRIVATE -gcodeview)
elseif(MSVC)
    target_link_options(tdsweb PRIVATE /DEBUG)
endif()

if(MSVC)
    target_compile_options(tdsweb PRIVATE /W4 /EHsc)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(WIN32)
        target_compile_options(tdsweb PRIVATE -Wall -Wextra -Wno-address-of-packed-member -Wconversion -gcodeview)
    else()
        target_compile_options(tdsweb PRIVATE -Wall -Wextra -Wno-address-of-packed-member)
    endif()
endif()

install(TARGETS tdsweb DESTINATION bin)
