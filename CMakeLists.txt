cmake_minimum_required(VERSION 3.21)
project(DupFind VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(USE_NATIVE_OPTIMIZATION "Optimize for the local machine" OFF)

# WIndows用アイコン
set(app_icon "${CMAKE_CURRENT_SOURCE_DIR}/icons/DupFind.ico")

if(MSVC)
    set(X_VCPKG_APPLOCAL_DEPS_INSTALL ON)
endif()

# Qt 6
find_package(Qt6 COMPONENTS Widgets Concurrent Linguist Network REQUIRED)
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
        Qt6::Network
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
#add_executable(${PROJECT_NAME} src/main.cpp)
qt_add_executable(${PROJECT_NAME}
    src/main.cpp
    ${app_icon}
    resources.qrc
)

# CLI target
add_executable(DupFindCmd src/CmdMain.cpp)
target_link_libraries(DupFindCmd PRIVATE DupFindCore)

# 最適化オプション
set(ALL_TARGETS ${PROJECT_NAME} DupFindCmd DupFindCore)

if(MSVC)
    foreach(target IN LISTS ALL_TARGETS)
        target_compile_options(${target} PRIVATE /arch:AVX2)
        
        target_compile_options(${target} PRIVATE
            $<$<NOT:$<CONFIG:Debug>>:
                /O2 /Oi /GL /fp:fast
            >
        )
        
        target_link_options(${target} PRIVATE 
            $<$<NOT:$<CONFIG:Debug>>:
                /LTCG /OPT:REF /OPT:ICF
            >
        )
    endforeach()

elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64")
        foreach(target IN LISTS ALL_TARGETS)
            target_compile_options(${target} PRIVATE -mpopcnt -msse4.2)
        endforeach()
    endif()
    if(USE_NATIVE_OPTIMIZATION)
        foreach(target IN LISTS ALL_TARGETS)
            target_compile_options(${target} PRIVATE -march=native)
        endforeach()
    endif()    
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE DupFindCore)

set_target_properties(${PROJECT_NAME} PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

# Translations
qt_add_translations(${PROJECT_NAME}
    TS_FILES 
        translations/dupfind_en.ts
        translations/dupfind_ja.ts
        translations/dupfind_zh_CN.ts
        translations/dupfind_zh_TW.ts
        translations/dupfind_es.ts
        translations/dupfind_de.ts
        translations/dupfind_ru.ts
        translations/dupfind_pt.ts
        translations/dupfind_pl.ts
        translations/dupfind_ko.ts
    RESOURCE i18n
)


# -----------------------------------------------------------------------------
# Installation
# -----------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME} DupFindCmd
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
# Desktop file and Icon (only for Linux)
if(NOT WIN32)
    install(FILES debian/dupfind.desktop DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
    install(FILES Deploy/Assets/Square150x150Logo.png 
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/pixmaps 
            RENAME dupfind.png)
endif()

# -----------------------------------------------------------------------------
# Deployment Configuration (Windows)
# -----------------------------------------------------------------------------
if(WIN32)
    # UCRTライブラリを自動的に含める
    set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)
    include(InstallRequiredSystemLibraries)
    # vc_redist.x64.exeを外す
    if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
        list(FILTER CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS EXCLUDE REGEX "\\.exe$")
    endif()
    
    # Qt6::qmake 経由で bin ディレクトリを特定し windeployqt を探す
    get_target_property(_qmake_exe Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_exe}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")

    set(DEPLOY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Deploy/DupFind")

    # Appx Manifest Generation
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/Deploy/AppxManifest.xml.in"
        "${CMAKE_CURRENT_BINARY_DIR}/AppxManifest.xml"
        @ONLY
    )

    # Path to makeappx.exe (Windows SDK Build Tools)
    set(MAKEAPPX_EXECUTABLE "C:/Program Files (x86)/Microsoft Visual Studio/Shared/NuGetPackages/microsoft.windows.sdk.buildtools/10.0.26100.1742/bin/10.0.26100.0/x64/makeappx.exe")

    # `cmake --build build --target deploy` で起動する専用ターゲット
    add_custom_target(deploy
        # 1. 展開用ディレクトリの作成
        COMMAND ${CMAKE_COMMAND} -E make_directory "${DEPLOY_DIR}"
        
        # 2. 実行ファイル本体のコピー
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${PROJECT_NAME}>" "${DEPLOY_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:DupFindCmd>" "${DEPLOY_DIR}/"
        
# 3. リソースディレクトリのコピーは不要になったため削除
        
        # 4. vcpkgがビルドディレクトリに出力した サードパーティDLL (OpenCV, SQLiteなど) の収集
        COMMAND powershell -NoProfile -Command "Copy-Item -Path '$<TARGET_FILE_DIR:${PROJECT_NAME}>/*.dll' -Destination '${DEPLOY_DIR}/' -Force -ErrorAction SilentlyContinue"

        # UCRTライブラリのコピー
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS} "${DEPLOY_DIR}/"
        
        # 5. windeployqt による Qt DLL (--compiler-runtime) の自動収集
        COMMAND "${WINDEPLOYQT_EXECUTABLE}" --dir "${DEPLOY_DIR}" "${DEPLOY_DIR}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
        
        # 6. vc_redist.x64.exe の削除 (Appxには不要)
        COMMAND ${CMAKE_COMMAND} -E rm -f "${DEPLOY_DIR}/vc_redist.x64.exe"

        # 7. Appx用リソースとManifestのコピー
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_BINARY_DIR}/AppxManifest.xml" "${DEPLOY_DIR}/"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/Deploy/Assets" "${DEPLOY_DIR}/Assets"

        # 8. Appxパッケージの作成
        COMMAND "${MAKEAPPX_EXECUTABLE}" pack /d "${DEPLOY_DIR}" /p "${CMAKE_CURRENT_SOURCE_DIR}/Deploy/DupFind.appx" /l /o

        DEPENDS ${PROJECT_NAME} DupFindCmd
        COMMENT "Deploying and creating Appx package in ${CMAKE_CURRENT_SOURCE_DIR}/Deploy/DupFind.appx..."
    )
endif()
