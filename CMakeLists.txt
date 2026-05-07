cmake_minimum_required(VERSION 3.14)
project(RPS LANGUAGES C CXX)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/out/bin")

add_executable(RPS
    main.cpp
    db.cpp
    rps_ui.cpp
    rps_database.cpp
    rps_simulation.cpp
    bsp.cpp
    bot.cpp
    tigr.c
)

target_include_directories(RPS PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/.. ${teenyat_SOURCE_DIR})

target_compile_options(RPS PRIVATE $<$<NOT:$<CONFIG:Debug>>:-O3>)

# tigr.c is noisy with -Wall -Wextra -Wpedantic, so keep those warnings
# enabled for the C++ sources only.
set(WARNING_OPTIONS -Wall -Wextra -Wpedantic)
set(WARNING_SOURCES
    main.cpp
    db.cpp
    rps_ui.cpp
    rps_database.cpp
    rps_simulation.cpp
    bsp.cpp
    bot.cpp
)
set_property(SOURCE ${WARNING_SOURCES} PROPERTY COMPILE_OPTIONS ${WARNING_OPTIONS})

set(OpenGL_GL_PREFERENCE LEGACY)
find_package(OpenGL REQUIRED)

# Fetch TeenyAT from GitHub and build a local static target `teenyat`.
include(FetchContent)
## Allow overriding the TeenyAT repository URL via the TEENYAT_REPO env var
if(DEFINED ENV{TEENYAT_REPO})
    set(TEENYAT_REPO $ENV{TEENYAT_REPO})
else()
    set(TEENYAT_REPO https://github.com/miniat-amos/TeenyAT.git)
endif()

FetchContent_Declare(
    teenyat
    GIT_REPOSITORY ${TEENYAT_REPO}
    GIT_TAG main
)
FetchContent_MakeAvailable(teenyat)

add_library(teenyat_lib STATIC ${teenyat_SOURCE_DIR}/teenyat.c)
target_include_directories(teenyat_lib PUBLIC ${teenyat_SOURCE_DIR})

target_link_libraries(RPS PRIVATE teenyat_lib ${OPENGL_gl_LIBRARY})

if(WIN32)
    target_link_libraries(RPS PRIVATE gdi32)
elseif(APPLE)
    find_library(OpenGL_LIBRARY OpenGL)
    find_library(Cocoa_LIBRARY Cocoa)
    target_link_libraries(RPS PRIVATE ${OpenGL_LIBRARY} ${Cocoa_LIBRARY})
elseif(UNIX)
    find_package(X11 REQUIRED)
    target_include_directories(RPS PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(RPS PRIVATE GLU GL X11)
endif()

add_custom_command(TARGET RPS POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets
            $<TARGET_FILE_DIR:RPS>/assets
)

add_custom_command(TARGET RPS POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_BINARY_DIR}/out/bin/tnasm.exe
            $<TARGET_FILE_DIR:RPS>/tnasm.exe
    COMMENT "Copying tnasm.exe to output directory"
)

set(RPS_ENV_FILE "${CMAKE_BINARY_DIR}/out/bin/.env")
if(NOT EXISTS "${RPS_ENV_FILE}")
    file(MAKE_DIRECTORY "${RPS_ENV_DIR}")
    file(WRITE "${RPS_ENV_FILE}"
"DB_HOST=localhost\n"
"DB_PORT=5432\n"
"DB_NAME=rps\n"
"DB_USER=rps_app\n"
"DB_PASS=\n")
endif()