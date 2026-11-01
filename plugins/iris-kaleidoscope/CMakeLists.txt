# Iris Kaleidoscope（瞳の万華鏡）After Effects プラグイン
#
#   Windows: cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DAE_SDK_DIR="C:/AfterEffectsSDK/Examples"
#            cmake --build build --config Release        → build/Release/IrisKaleidoscope.aex
#   macOS  : cmake -S . -B build -G Xcode -DAE_SDK_DIR="$HOME/AfterEffectsSDK/Examples"
#            cmake --build build --config Release        → build/Release/IrisKaleidoscope.plugin
#
# AE_SDK_DIR は SDK の Examples フォルダ（Headers / Util / Resources が入っている所）。
cmake_minimum_required(VERSION 3.20)
project(IrisKaleidoscope LANGUAGES C CXX)

set(AE_SDK_DIR "" CACHE PATH "After Effects SDK の Examples フォルダ")
if(NOT EXISTS "${AE_SDK_DIR}/Headers/AE_Effect.h")
  message(FATAL_ERROR "AE_SDK_DIR に After Effects SDK の Examples フォルダを指定してください (現在: '${AE_SDK_DIR}')")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SDK_INC
  "${AE_SDK_DIR}/Headers"
  "${AE_SDK_DIR}/Headers/SP"
  "${AE_SDK_DIR}/Util"
  "${AE_SDK_DIR}/Resources")
set(SDK_UTIL_SRC)   # SDK の Util ソースを使わない（ヘッダのみ）
set(PLUGIN_SRC
  src/Kaleido.cpp
  src/Kaleido.h
  src/KaleidoCore.cpp
  src/KaleidoCore.h)
set(PIPL_R "${CMAKE_CURRENT_SOURCE_DIR}/src/KaleidoPiPL.r")

if(WIN32)
  if(EXISTS "${AE_SDK_DIR}/Headers/Win")
    list(APPEND SDK_INC "${AE_SDK_DIR}/Headers/Win")
  endif()
  # PiPL: .r → (cl /EP) → .rr → (PiPLTool) → .rrc → (cl /EP) → .rc
  set(PIPL_RR  "${CMAKE_CURRENT_BINARY_DIR}/KaleidoPiPL.rr")
  set(PIPL_RRC "${CMAKE_CURRENT_BINARY_DIR}/KaleidoPiPL.rrc")
  set(PIPL_RC  "${CMAKE_CURRENT_BINARY_DIR}/KaleidoPiPL.rc")
  add_custom_command(
    OUTPUT "${PIPL_RC}"
    COMMAND cl /nologo /I "${AE_SDK_DIR}/Headers" /EP "${PIPL_R}" > "${PIPL_RR}"
    COMMAND "${AE_SDK_DIR}/Resources/PiPLTool.exe" "${PIPL_RR}" "${PIPL_RRC}"
    COMMAND cl /nologo /D "MSWindows" /EP "${PIPL_RRC}" > "${PIPL_RC}"
    DEPENDS "${PIPL_R}"
    COMMENT "Compiling the PiPL"
    VERBATIM)

  add_library(IrisKaleidoscope MODULE ${PLUGIN_SRC} ${SDK_UTIL_SRC} "${PIPL_RC}")
  target_include_directories(IrisKaleidoscope PRIVATE src ${SDK_INC})
  target_compile_definitions(IrisKaleidoscope PRIVATE MSWindows WIN32 _WINDOWS _CRT_SECURE_NO_WARNINGS NOMINMAX)
  target_compile_options(IrisKaleidoscope PRIVATE /utf-8 /MP)
  set_target_properties(IrisKaleidoscope PROPERTIES SUFFIX ".aex" PREFIX "")
  # 静的ランタイムにして配布しやすくする
  set_property(TARGET IrisKaleidoscope PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

elseif(APPLE)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)

  # PiPL: Rez で .rsrc を作り、バンドルの Resources に入れる
  set(PIPL_RSRC "${CMAKE_CURRENT_BINARY_DIR}/IrisKaleidoscope.rsrc")
  add_custom_command(
    OUTPUT "${PIPL_RSRC}"
    COMMAND xcrun Rez -useDF -d __MACH__ -d AE_OS_MAC
            -arch arm64 -arch x86_64
            -i "${AE_SDK_DIR}/Headers" -i "${AE_SDK_DIR}/Resources" -i "${AE_SDK_DIR}/Util"
            -o "${PIPL_RSRC}" "${PIPL_R}"
    DEPENDS "${PIPL_R}"
    COMMENT "Compiling the PiPL"
    VERBATIM)
  set_source_files_properties("${PIPL_RSRC}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

  add_library(IrisKaleidoscope MODULE ${PLUGIN_SRC} ${SDK_UTIL_SRC} "${PIPL_RSRC}")
  target_include_directories(IrisKaleidoscope PRIVATE src ${SDK_INC})
  set_target_properties(IrisKaleidoscope PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "plugin"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/mac/Info.plist.in"
    XCODE_ATTRIBUTE_GENERATE_PKGINFO_FILE YES
    XCODE_ATTRIBUTE_WRAPPER_EXTENSION "plugin")
  target_link_libraries(IrisKaleidoscope PRIVATE "-framework CoreFoundation")
  # PkgInfo（Xcode 以外のジェネレータ向け）
  add_custom_command(TARGET IrisKaleidoscope POST_BUILD
    COMMAND /bin/sh -c "printf 'eFKTFXTC' > \"$<TARGET_BUNDLE_CONTENT_DIR:IrisKaleidoscope>/PkgInfo\""
    VERBATIM)
else()
  message(FATAL_ERROR "After Effects プラグインは Windows か macOS でビルドしてください。"
                      " Linux では test/ のテストツールだけビルドできます。")
endif()
