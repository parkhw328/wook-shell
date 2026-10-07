cmake_minimum_required(VERSION 3.24)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" WSHELL_VERSION LIMIT_COUNT 1)
project(WookShell VERSION ${WSHELL_VERSION} LANGUAGES C CXX RC)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION")
configure_file(assets/version.h.in "${CMAKE_CURRENT_SOURCE_DIR}/build/version.h" @ONLY)
configure_file(assets/app.manifest "${CMAKE_CURRENT_SOURCE_DIR}/build/app.manifest" @ONLY)
if(NOT TARGET wshell-terminal)
    message(FATAL_ERROR "Use scripts/build.ps1 to build wShell with its statically linked terminal.")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
add_library(wook-core STATIC src/core.cpp src/store.c src/backup.cpp src/credentials.cpp)
target_link_libraries(wook-core PUBLIC crypt32)
target_include_directories(wook-core PUBLIC src)
target_compile_options(wook-core PRIVATE -Wall -Wextra)
add_library(wsftp STATIC mac/Sources/CSFTP/wsftp.c)
target_include_directories(wsftp PUBLIC mac/Sources/CSFTP/include)
target_compile_options(wsftp PRIVATE -Wall -Wextra)
set(workspace_sources src/main.cpp src/dialog.cpp src/resources.cpp src/key_dialog.cpp src/settings_ui.cpp src/ime.c src/prompt.cpp src/sftp.cpp src/sftp_view.cpp assets/app.rc
    $<TARGET_OBJECTS:wshell-terminal> $<TARGET_OBJECTS:wshell-terminal-be-list>)
# windres does not emit dependencies for binary RCDATA and nested resource scripts.
set_property(SOURCE assets/app.rc APPEND PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/app.manifest"
    "${CMAKE_CURRENT_SOURCE_DIR}/build/app.manifest"
    "${CMAKE_CURRENT_SOURCE_DIR}/build/version.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/wshell.ico"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/JetBrainsMono-Regular.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/JetBrainsMono-Bold.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/build/legal-notices.txt"
    "${CMAKE_SOURCE_DIR}/windows/wshell-terminal.rc"
    "${CMAKE_SOURCE_DIR}/windows/wshell-common.rc2"
    "${CMAKE_SOURCE_DIR}/windows/wshell.ico")
set(workspace_libraries wook-core wsftp wshell-keys guiterminal guimisc eventloop sshclient otherbackends settings network keygen crypto utils
    comctl32 dwmapi uxtheme comdlg32 shell32 gdi32 user32 ole32 advapi32 imm32 ws2_32 bcrypt)
add_executable(WookShell WIN32 ${workspace_sources})
set_target_properties(WookShell PROPERTIES OUTPUT_NAME wShell)
target_link_libraries(WookShell PRIVATE ${workspace_libraries})
target_compile_options(WookShell PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
target_link_options(WookShell PRIVATE -municode -static -Wl,--nxcompat,--dynamicbase,--high-entropy-va -s)
enable_testing()
add_executable(sftp-codec-tests tests/sftp_codec_test.cpp)
target_link_libraries(sftp-codec-tests PRIVATE wsftp)
target_link_options(sftp-codec-tests PRIVATE -static)
add_test(NAME sftp-codec COMMAND sftp-codec-tests)
add_executable(sftp-tests tests/sftp_test.cpp src/sftp.cpp)
target_compile_definitions(sftp-tests PRIVATE WOOK_SFTP_TEST)
target_link_libraries(sftp-tests PRIVATE wook-core wsftp shell32 ole32)
target_link_options(sftp-tests PRIVATE -municode -static)
add_executable(core-tests tests/core_test.cpp)
target_link_libraries(core-tests PRIVATE wook-core shell32)
target_link_options(core-tests PRIVATE -municode -static)
add_test(NAME core COMMAND core-tests)
add_executable(ime-tests tests/ime_test.cpp src/ime.c)
target_include_directories(ime-tests PRIVATE src)
target_link_libraries(ime-tests PRIVATE utils imm32 gdi32 user32)
target_link_options(ime-tests PRIVATE -static)
add_test(NAME ime COMMAND ime-tests)
add_executable(ui-smoke-tests WIN32 ${workspace_sources})
target_compile_definitions(ui-smoke-tests PRIVATE WOOK_UI_TEST)
target_link_libraries(ui-smoke-tests PRIVATE ${workspace_libraries})
target_link_options(ui-smoke-tests PRIVATE -municode -static)
