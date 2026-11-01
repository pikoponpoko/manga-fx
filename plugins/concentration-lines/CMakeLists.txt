# Concentration Lines — After Effects エフェクトプラグイン
#
#   cmake -B build -DAE_SDK_ROOT=<AfterEffectsSDK>/Examples
#   cmake --build build --config Release
#
# Windows: Visual Studio 2022 (x64)   → build/Release/ConcentrationLines.aex
# macOS:   Xcode (arm64 + x86_64)     → build/Release/ConcentrationLines.plugin
cmake_minimum_required(VERSION 3.20)
project(ConcentrationLines CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(AE_SDK_ROOT "" CACHE PATH "After Effects SDK の Examples フォルダ")
if(NOT EXISTS "${AE_SDK_ROOT}/Headers/AE_Effect.h")
    message(FATAL_ERROR "AE_SDK_ROOT に SDK の Examples フォルダを指定してください (Headers/AE_Effect.h が見つかりません)")
endif()

set(SDK_INCLUDES
    "${AE_SDK_ROOT}/Headers"
    "${AE_SDK_ROOT}/Headers/SP"
    "${AE_SDK_ROOT}/Headers/Win"
    "${AE_SDK_ROOT}/Util"
    "${AE_SDK_ROOT}/Resources")

set(SOURCES
    src/ConcentrationLines.cpp
    src/ConcentrationLines.h
    src/ConcentrationLinesCore.h
    "${AE_SDK_ROOT}/Util/AEGP_SuiteHandler.cpp"
    "${AE_SDK_ROOT}/Util/MissingSuiteError.cpp")

set(PIPL_SRC "${CMAKE_CURRENT_SOURCE_DIR}/src/ConcentrationLinesPiPL.r")

if(WIN32)
    enable_language(RC)
    # .r → (プリプロセス) → PiPLtool → (プリプロセス) → .rc
    set(PIPL_RR  "${CMAKE_CURRENT_BINARY_DIR}/ConcentrationLinesPiPL.rr")
    set(PIPL_RRC "${CMAKE_CURRENT_BINARY_DIR}/ConcentrationLinesPiPL.rrc")
    set(PIPL_RC  "${CMAKE_CURRENT_BINARY_DIR}/ConcentrationLinesPiPL.rc")
    set(PIPL_INC "")
    foreach(d ${SDK_INCLUDES})
        list(APPEND PIPL_INC "/I${d}")
    endforeach()
    find_program(PIPLTOOL PiPLtool.exe PATHS "${AE_SDK_ROOT}/Resources" REQUIRED NO_DEFAULT_PATH)
    add_custom_command(
        OUTPUT "${PIPL_RC}"
        COMMAND "${CMAKE_CXX_COMPILER}" ${PIPL_INC} /D MSWindows /D AE_OS_WIN /D AE_PROC_INTELx64 /EP "${PIPL_SRC}" > "${PIPL_RR}"
        COMMAND "${PIPLTOOL}" "${PIPL_RR}" "${PIPL_RRC}"
        COMMAND "${CMAKE_CXX_COMPILER}" /D MSWindows /EP "${PIPL_RRC}" > "${PIPL_RC}"
        DEPENDS "${PIPL_SRC}"
        COMMENT "PiPL リソースを生成中"
        VERBATIM)
    add_library(ConcentrationLines MODULE ${SOURCES} "${PIPL_RC}")
    set_target_properties(ConcentrationLines PROPERTIES SUFFIX ".aex")
    target_compile_definitions(ConcentrationLines PRIVATE MSWindows WIN32 _WINDOWS _CRT_SECURE_NO_WARNINGS)
    target_compile_options(ConcentrationLines PRIVATE /MP /utf-8)
    set_property(TARGET ConcentrationLines PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
elseif(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
    set(PIPL_RSRC "${CMAKE_CURRENT_BINARY_DIR}/ConcentrationLines.rsrc")
    set(REZ_INC "")
    foreach(d ${SDK_INCLUDES})
        list(APPEND REZ_INC -i "${d}")
    endforeach()
    add_custom_command(
        OUTPUT "${PIPL_RSRC}"
        COMMAND xcrun Rez ${REZ_INC} -d AE_OS_MAC -d __MACH__ -useDF -o "${PIPL_RSRC}" "${PIPL_SRC}"
        DEPENDS "${PIPL_SRC}"
        COMMENT "PiPL リソースを生成中"
        VERBATIM)
    add_library(ConcentrationLines MODULE ${SOURCES} "${PIPL_RSRC}")
    set_source_files_properties("${PIPL_RSRC}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(ConcentrationLines PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "plugin"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/mac/Info.plist"
        XCODE_ATTRIBUTE_WRAPPER_EXTENSION "plugin"
        XCODE_ATTRIBUTE_GENERATE_PKGINFO_FILE "YES")
    target_link_libraries(ConcentrationLines PRIVATE "-framework CoreFoundation")
else()
    message(FATAL_ERROR "After Effects プラグインは Windows か macOS でビルドしてください")
endif()

target_include_directories(ConcentrationLines PRIVATE src ${SDK_INCLUDES})
