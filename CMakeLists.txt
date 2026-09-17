cmake_minimum_required(VERSION 3.15)
project(ClipVault LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
  message(FATAL_ERROR "ClipVault builds only on Windows")
endif()

if (MSVC)
  enable_language(RC)
else()
  enable_language(RC)  # windres for MinGW
endif()

add_executable(ClipVault WIN32
  src/main.cpp
  src/app.cpp
  src/capture.cpp
  src/restore.cpp
  src/imaging.cpp
  src/history.cpp
  src/storage.cpp
  src/settings.cpp
  src/theme.cpp
  src/util.cpp
  src/popup.cpp
  src/settings_window.cpp
  resources/app.rc
)

target_compile_definitions(ClipVault PRIVATE
  UNICODE _UNICODE
  WIN32_LEAN_AND_MEAN NOMINMAX
  _CRT_SECURE_NO_WARNINGS
)

# inbox system libraries only — no third-party dependencies
target_link_libraries(ClipVault PRIVATE
  gdi32 user32 shell32 advapi32 comctl32 dwmapi uxtheme gdiplus
)

if (MSVC)
  target_compile_options(ClipVault PRIVATE /O2 /EHsc /W4)
  set_property(TARGET ClipVault PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
  target_compile_options(ClipVault PRIVATE -O2 -Wall -Wextra -Wno-unused-parameter)
  # fully static: no libstdc++/libgcc/libwinpthread DLLs needed at runtime
  target_link_options(ClipVault PRIVATE -static -static-libgcc -static-libstdc++ -municode -s)
  # MinGW's toolchain auto-links a bare default manifest; shadow it with ours
  # (PerMonitorV2 + comctl6) by placing our compiled manifest first on the
  # startfile search path.
  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/default-manifest.o
    COMMAND ${CMAKE_RC_COMPILER} ${CMAKE_CURRENT_SOURCE_DIR}/resources/manifest_only.rc
            -O coff -o ${CMAKE_CURRENT_BINARY_DIR}/default-manifest.o
    DEPENDS resources/manifest_only.rc resources/app.manifest
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/resources
    COMMENT "Compiling application manifest"
  )
  add_custom_target(manifest_obj DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/default-manifest.o)
  add_dependencies(ClipVault manifest_obj)
  target_link_options(ClipVault PRIVATE "-B${CMAKE_BINARY_DIR}")
endif()

set_target_properties(ClipVault PROPERTIES OUTPUT_NAME "ClipVault")
