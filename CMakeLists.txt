cmake_minimum_required(VERSION 3.16)
project(guydances LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/glfw EXCLUDE_FROM_ALL)

add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/dance_data.h
    COMMAND python3 ${CMAKE_SOURCE_DIR}/tools/bin2c.py
            ${CMAKE_SOURCE_DIR}/assets/dance.bin ${CMAKE_BINARY_DIR}/dance_data.h
    DEPENDS ${CMAKE_SOURCE_DIR}/assets/dance.bin ${CMAKE_SOURCE_DIR}/tools/bin2c.py
    VERBATIM)
add_custom_target(dance_header DEPENDS ${CMAKE_BINARY_DIR}/dance_data.h)

set(SOURCES
    src/main.cpp
    src/sprite.cpp
    src/single_instance.cpp)

if(WIN32)
    list(APPEND SOURCES src/audio_windows.cpp)
else()
    list(APPEND SOURCES src/audio_linux.cpp)
endif()

add_executable(guydances ${SOURCES} ${CMAKE_BINARY_DIR}/dance_data.h)
add_dependencies(guydances dance_header)
target_include_directories(guydances PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_BINARY_DIR})
target_link_libraries(guydances PRIVATE glfw)

find_package(Threads REQUIRED)
target_link_libraries(guydances PRIVATE Threads::Threads)

if(WIN32)
    target_compile_definitions(guydances PRIVATE _WIN32_WINNT=0x0601)
    target_link_libraries(guydances PRIVATE opengl32 gdi32 ole32 user32 shell32)
    set_target_properties(guydances PROPERTIES WIN32_EXECUTABLE TRUE)
    if(MINGW)
        target_link_options(guydances PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
else()
    find_package(OpenGL REQUIRED)
    find_package(X11 REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse)
    target_include_directories(guydances PRIVATE
        ${X11_X11_INCLUDE_PATH}
        ${X11_Xrandr_INCLUDE_PATH}
        ${X11_Xinerama_INCLUDE_PATH}
        ${X11_Xcursor_INCLUDE_PATH}
        ${X11_Xi_INCLUDE_PATH}
        ${X11_Xshape_INCLUDE_PATH})
    target_link_libraries(guydances PRIVATE OpenGL::GL X11::X11 PkgConfig::PULSE)
endif()
