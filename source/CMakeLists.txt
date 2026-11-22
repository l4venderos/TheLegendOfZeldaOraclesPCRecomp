cmake_minimum_required(VERSION 3.20)
project(OraclesPCRecomp VERSION 0.2.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(oracles_compiler STATIC
    compiler/rom.cpp
    compiler/sm83.cpp
    compiler/analysis.cpp
    compiler/emitter.cpp
)
target_include_directories(oracles_compiler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(oracles-recompiler compiler/main.cpp)
target_link_libraries(oracles-recompiler PRIVATE oracles_compiler)

add_executable(oracles-core-tests tests/core_tests.cpp compiler/sm83.cpp compiler/analysis.cpp compiler/rom.cpp)
target_include_directories(oracles-core-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
enable_testing()
add_test(NAME sm83-decoder-smoke COMMAND oracles-core-tests)

add_executable(oracles-render-layout-tests tests/render_layout_tests.cpp)
target_include_directories(oracles-render-layout-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME renderer-aspect-layout COMMAND oracles-render-layout-tests)

add_executable(oracles-input-bindings-tests tests/input_bindings_tests.cpp)
target_include_directories(oracles-input-bindings-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME keyboard-bindings COMMAND oracles-input-bindings-tests)

add_executable(oracles-runtime-tests tests/runtime_tests.cpp runtime/runtime.cpp runtime/audio.cpp runtime/game_runner.cpp ppu/ppu.cpp)
target_include_directories(oracles-runtime-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME runtime-smoke COMMAND oracles-runtime-tests)

# Generate and compile a regression fixture containing two POP AF operations
# in a single native block. This catches duplicate temporary declarations in
# emitted C++ during every normal compiler build, including MSVC/NMake builds.
add_executable(oracles-emitter-regression-generator tests/emitter_regression.cpp)
target_link_libraries(oracles-emitter-regression-generator PRIVATE oracles_compiler)
set(EMITTER_REGRESSION_CPP "${CMAKE_CURRENT_BINARY_DIR}/emitter_regression_generated.cpp")
add_custom_command(
    OUTPUT "${EMITTER_REGRESSION_CPP}"
    COMMAND $<TARGET_FILE:oracles-emitter-regression-generator> "${EMITTER_REGRESSION_CPP}"
    DEPENDS oracles-emitter-regression-generator
    VERBATIM
)
set_source_files_properties("${EMITTER_REGRESSION_CPP}" PROPERTIES GENERATED TRUE)
add_library(oracles-emitter-regression OBJECT "${EMITTER_REGRESSION_CPP}")
target_include_directories(oracles-emitter-regression PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(oracles-emitter-regression PRIVATE cxx_std_20)
add_test(NAME native-emitter-pop-af-scope
         COMMAND oracles-emitter-regression-generator "${CMAKE_CURRENT_BINARY_DIR}/emitter_regression_test.cpp")

# One explicit target for the complete portable test executable set. The
# Windows batch build uses this target before invoking CTest, so a newly added
# test cannot be registered but accidentally left unbuilt.
add_custom_target(oracles-test-binaries
    DEPENDS
        oracles-core-tests
        oracles-render-layout-tests
        oracles-input-bindings-tests
        oracles-runtime-tests
        oracles-emitter-regression
)

option(ORACLES_BUILD_GAME "Build the combined Oracle of Ages/Seasons native executable" OFF)
set(AGES_NATIVE "" CACHE FILEPATH "Generated Oracle of Ages native blocks")
set(SEASONS_NATIVE "" CACHE FILEPATH "Generated Oracle of Seasons native blocks")

if(ORACLES_BUILD_GAME)
    if(NOT EXISTS "${AGES_NATIVE}")
        message(FATAL_ERROR "AGES_NATIVE does not exist. Run compile.bat (Windows) or compile.sh (Linux) so the independent recompiler can generate it.")
    endif()
    if(NOT EXISTS "${SEASONS_NATIVE}")
        message(FATAL_ERROR "SEASONS_NATIVE does not exist. Run compile.bat (Windows) or compile.sh (Linux) so the independent recompiler can generate it.")
    endif()

    if(WIN32)
        add_executable(Oracles WIN32
            app/main_win32.cpp
            renderer/d3d11_renderer.cpp
            runtime/runtime.cpp
            runtime/audio.cpp
            platform/waveout_audio.cpp
            runtime/game_runner.cpp
            ppu/ppu.cpp
            "${AGES_NATIVE}"
            "${SEASONS_NATIVE}"
        )
        target_link_libraries(Oracles PRIVATE user32 gdi32 Xinput9_1_0 d3d11 dxgi d3dcompiler winmm)
        target_compile_definitions(Oracles PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)
    else()
        add_executable(Oracles
            app/main_cli.cpp
            runtime/runtime.cpp
            runtime/audio.cpp
            runtime/game_runner.cpp
            ppu/ppu.cpp
            "${AGES_NATIVE}"
            "${SEASONS_NATIVE}"
        )
    endif()
    target_include_directories(Oracles PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

    # The generated Ages/Seasons translation units contain thousands of
    # machine-generated basic-block functions. MSVC's default COFF object
    # format section limit (65,279) can be exceeded by these files (C1128).
    # Apply /bigobj only to the generated translation units; hand-written
    # runtime/frontend sources do not need the larger object format.
    if(MSVC)
        set_source_files_properties(
            "${AGES_NATIVE}" "${SEASONS_NATIVE}"
            PROPERTIES COMPILE_OPTIONS "/bigobj"
        )
        message(STATUS "MSVC generated game sources: enabling /bigobj for Ages and Seasons")
    endif()

    set_target_properties(Oracles PROPERTIES
        OUTPUT_NAME "Oracles"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/../dist"
    )
    foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        set_target_properties(Oracles PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_BINARY_DIR}/../dist")
    endforeach()
endif()
