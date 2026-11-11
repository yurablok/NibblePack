cmake_minimum_required(VERSION 3.12)

project(NibblePack C CXX)

set(NibblePackCppLib ON)

if("${CMAKE_SOURCE_DIR}" STREQUAL "${CMAKE_CURRENT_SOURCE_DIR}")
    message(STATUS "NibblePack Development")
    include("CMakeUtils.cmake")

    init_project(
        "TestNibblePackCpp"
        "TestNibblePackCpp11"
    )

    set(NibblePackCppTests ON)
    set(BUILD_SHARED_LIBS FALSE)
endif()

if(NibblePackCppLib)
    add_library(NibblePackCpp STATIC
        "cpp/NibblePack.cpp"
        "cpp/NibblePack.hpp"
        "cpp/fp_t.hpp"
    )
    set_target_properties(NibblePackCpp PROPERTIES LINKER_LANGUAGE CXX)
    target_include_directories(NibblePackCpp PUBLIC
        "cpp"
        "deps/string-view-lite/include/nonstd"
    )

    if(NibblePackCppTests)
        set(MSGPACK_BUILD_EXAMPLES OFF)
        fetch_git("deps/msgpack-c"
            https://github.com/msgpack/msgpack-c.git c-7.0.2 NO)
        add_subdirectory("deps/msgpack-c")

        fetch_git("deps/string-view-lite"
            https://github.com/martinmoene/string-view-lite.git v1.8.2 NO)

        fetch_git("deps/cpp-msgpack-without-dom"
            https://github.com/yurablok/cpp-msgpack-without-dom.git main NO)

        add_subdirectory("deps/cpp-msgpack-without-dom")

        add_executable(TestNibblePackCpp
            "cpp/tests.cpp"
        )
        target_link_libraries(TestNibblePackCpp PRIVATE
            NibblePackCpp
            cpp-msgpack-without-dom
        )
        if(MSVC)
            target_compile_options(TestNibblePackCpp PRIVATE
                /W4 # Warning Level
            )
        else()
            target_compile_options(TestNibblePackCpp PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Wno-float-conversion
            )
        endif()

        add_executable(TestNibblePackCpp11
            "cpp/tests.cpp"
            "cpp/NibblePack.cpp"
            "cpp/fp_t.hpp"
        )
        target_include_directories(TestNibblePackCpp11 PRIVATE
            "cpp"
            "deps/string-view-lite/include/nonstd"
        )
        target_compile_definitions(TestNibblePackCpp11 PRIVATE
            NIBLP_TEST_NO_MSGPACK
        )
        set_target_properties(TestNibblePackCpp11 PROPERTIES CXX_STANDARD 11)
    endif()
endif()
