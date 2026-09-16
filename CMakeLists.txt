cmake_minimum_required(VERSION 3.16)

project(fieldpack VERSION 1.0.0 LANGUAGES CXX)

option(FIELDPACK_BUILD_TESTS "Build FieldPack Tcl tests" ON)
option(FIELDPACK_BUILD_BENCHMARKS "Build FieldPack benchmarks" OFF)

find_package(TCL QUIET)
if(NOT TCL_LIBRARY OR NOT TCL_INCLUDE_PATH)
    find_path(FIELDPACK_TCL_INCLUDE_DIR tcl.h REQUIRED)
    find_library(FIELDPACK_TCL_LIBRARY NAMES tcl tcl8.7 tcl8.6 REQUIRED)
endif()

function(fieldpack_link_tcl target)
    if(TARGET Tcl::Tcl)
        target_link_libraries(${target} PRIVATE Tcl::Tcl)
    elseif(TARGET TCL::TCL)
        target_link_libraries(${target} PRIVATE TCL::TCL)
    elseif(TCL_LIBRARY AND TCL_INCLUDE_PATH)
        target_include_directories(${target} PRIVATE ${TCL_INCLUDE_PATH})
        target_link_libraries(${target} PRIVATE ${TCL_LIBRARY})
    else()
        target_include_directories(${target} PRIVATE ${FIELDPACK_TCL_INCLUDE_DIR})
        target_link_libraries(${target} PRIVATE ${FIELDPACK_TCL_LIBRARY})
    endif()
endfunction()

add_library(fieldpack_core STATIC
    src/fieldpack/field_ops.cpp
    src/fieldpack/fieldpack.cpp
    src/fieldpack/registry.cpp
    src/fieldpack/schema.cpp
)
set_target_properties(fieldpack_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(fieldpack_core PUBLIC cxx_std_17)
target_include_directories(fieldpack_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(fieldpack_core PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CXX_COMPILER_ID:MSVC>:/O2>
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-O3>
)

add_library(fieldpack MODULE tcl/fieldpack.cpp)
target_compile_features(fieldpack PRIVATE cxx_std_17)
target_include_directories(fieldpack PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/tclxx/include)
target_link_libraries(fieldpack PRIVATE fieldpack_core)
fieldpack_link_tcl(fieldpack)
target_compile_options(fieldpack PRIVATE
    -Wall -Wextra -Wpedantic
    $<$<CXX_COMPILER_ID:MSVC>:/O2>
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-O3>
)
set_target_properties(fieldpack PROPERTIES PREFIX "" OUTPUT_NAME "fieldpack")

if(FIELDPACK_BUILD_BENCHMARKS)
    add_executable(fieldpack_cpp_time benchmark/fieldpack_cpp_time.cpp)
    target_compile_features(fieldpack_cpp_time PRIVATE cxx_std_17)
    target_link_libraries(fieldpack_cpp_time PRIVATE fieldpack_core)
    target_compile_options(fieldpack_cpp_time PRIVATE
        -Wall -Wextra -Wpedantic
        $<$<CXX_COMPILER_ID:MSVC>:/O2>
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-O3>
    )
endif()

if(FIELDPACK_BUILD_TESTS)
    enable_testing()
    add_executable(fieldpack_core_test tests/fieldpack.cpp)
    target_compile_features(fieldpack_core_test PRIVATE cxx_std_17)
    target_include_directories(fieldpack_core_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(fieldpack_core_test PRIVATE fieldpack_core)
    target_compile_options(fieldpack_core_test PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME fieldpack_core COMMAND fieldpack_core_test)

    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/tests/fieldpack.tcl ${CMAKE_CURRENT_BINARY_DIR}/fieldpack.tcl COPYONLY)
    add_test(NAME fieldpack_tcl COMMAND ${TCL_TCLSH} ${CMAKE_CURRENT_BINARY_DIR}/fieldpack.tcl $<TARGET_FILE:fieldpack>)
    set_tests_properties(fieldpack_tcl PROPERTIES ENVIRONMENT "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:fieldpack>:$ENV{LD_LIBRARY_PATH}")
endif()

install(TARGETS fieldpack
    LIBRARY DESTINATION lib/fieldpack
    RUNTIME DESTINATION lib/fieldpack
    ARCHIVE DESTINATION lib
)
install(TARGETS fieldpack_core ARCHIVE DESTINATION lib)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/tcl/pkgIndex.tcl DESTINATION lib/fieldpack)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/ DESTINATION include)
install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.md
    ${CMAKE_CURRENT_SOURCE_DIR}/README.md
    DESTINATION share/doc/fieldpack
)
