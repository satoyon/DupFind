cmake_minimum_required(VERSION 3.16)
project(DupFind VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 最適化と popcnt 命令出力のためのフラグ設定
if(MSVC)
    add_compile_options(/arch:AVX2)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-mpopcnt -msse4.2)
endif()

# Qt 6
find_package(Qt6 COMPONENTS Widgets Concurrent REQUIRED)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)


# OpenCVの前にProtobufを自力で見つけさせる
find_package(Protobuf REQUIRED)
# OpenCV
find_package(OpenCV REQUIRED)

# SQLite3
find_package(SQLite3 REQUIRED)

include_directories(include)

file(GLOB_RECURSE SOURCES src/*.cpp)
file(GLOB_RECURSE HEADERS include/*.hpp)

add_library(DupFindCore STATIC ${SOURCES} ${HEADERS})
target_link_libraries(DupFindCore
    PUBLIC 
        Qt6::Widgets
        Qt6::Concurrent
        ${OpenCV_LIBS}
        SQLite::SQLite3
)

# Filter out mains from the core library sources if possible, but CMake GLOB can't filter easily inline.
# Better approach: remove them explicitly.
get_target_property(CORE_SOURCES DupFindCore SOURCES)
list(REMOVE_ITEM CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")
list(REMOVE_ITEM CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/CmdMain.cpp")
set_target_properties(DupFindCore PROPERTIES SOURCES "${CORE_SOURCES}")

# GUI target
add_executable(${PROJECT_NAME} src/main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE DupFindCore)

set_target_properties(${PROJECT_NAME} PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

# CLI target
add_executable(DupFindCmd src/CmdMain.cpp)
target_link_libraries(DupFindCmd PRIVATE DupFindCore)

