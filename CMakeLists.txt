cmake_minimum_required(VERSION 3.24)
project(WookShell VERSION 0.2.0 LANGUAGES C CXX RC)
if(NOT TARGET wshell-terminal)
    message(FATAL_ERROR "Use scripts/build.ps1 to build wShell with its statically linked terminal.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
add_library(wook-core STATIC src/core.cpp src/store.c src/backup.cpp)
target_include_directories(wook-core PUBLIC src)
target_compile_options(wook-core PRIVATE -Wall -Wextra)
set(workspace_sources src/main.cpp src/dialog.cpp src/resources.cpp src/key_dialog.cpp assets/app.rc
    $<TARGET_OBJECTS:wshell-terminal> $<TARGET_OBJECTS:wshell-terminal-be-list>)
# windres does not emit dependencies for binary RCDATA and nested resource scripts.
set_property(SOURCE assets/app.rc APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/app.manifest"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/wshell.ico"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/branding/wshell-wordmark.png"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/JetBrainsMono-Regular.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/JetBrainsMono-Bold.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/build/legal-notices.txt"
    "${CMAKE_SOURCE_DIR}/windows/wshell-terminal.rc"
    "${CMAKE_SOURCE_DIR}/windows/wshell-common.rc2"
    "${CMAKE_SOURCE_DIR}/windows/wshell.ico")
set(workspace_libraries wook-core wshell-keys guiterminal guimisc eventloop sshclient otherbackends settings network keygen crypto utils
    comctl32 dwmapi uxtheme comdlg32 shell32 gdi32 user32 gdiplus ole32 advapi32 imm32 ws2_32 bcrypt)
add_executable(WookShell WIN32 ${workspace_sources})
set_target_properties(WookShell PROPERTIES OUTPUT_NAME wShell)
target_link_libraries(WookShell PRIVATE ${workspace_libraries})
target_compile_options(WookShell PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
target_link_options(WookShell PRIVATE -municode -static -Wl,--nxcompat,--dynamicbase,--high-entropy-va -s)
enable_testing()
add_executable(core-tests tests/core_test.cpp)
target_link_libraries(core-tests PRIVATE wook-core shell32)
target_link_options(core-tests PRIVATE -municode -static)
add_test(NAME core COMMAND core-tests)
add_executable(ui-smoke-tests WIN32 ${workspace_sources})
target_compile_definitions(ui-smoke-tests PRIVATE WOOK_UI_TEST)
target_link_libraries(ui-smoke-tests PRIVATE ${workspace_libraries})
target_link_options(ui-smoke-tests PRIVATE -municode -static)
