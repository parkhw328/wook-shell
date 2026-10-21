# Shared, explicit release settings for native and cross builds. Override stale
# cache / environment flags so a reused build directory cannot change the policy.
set(CMAKE_BUILD_TYPE Release CACHE STRING "wShell build configuration" FORCE)
set(CMAKE_C_FLAGS "" CACHE STRING "Common C flags" FORCE)
set(CMAKE_CXX_FLAGS "" CACHE STRING "Common C++ flags" FORCE)
set(CMAKE_RC_FLAGS "" CACHE STRING "Common resource flags" FORCE)
# PuTTY's setup.cmake removes NDEBUG from C to retain its security assertions.
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG" CACHE STRING "Release C flags" FORCE)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG" CACHE STRING "Release C++ flags" FORCE)
set(CMAKE_EXE_LINKER_FLAGS "-static -Wl,--nxcompat,--dynamicbase,--high-entropy-va,--no-insert-timestamp" CACHE STRING "Executable linker flags" FORCE)
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "" CACHE STRING "Additional release linker flags" FORCE)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "Keep upstream crypto optimization boundaries" FORCE)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Record actual compilation commands" FORCE)
