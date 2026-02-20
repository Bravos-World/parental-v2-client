cmake_minimum_required(VERSION 3.10)

if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project(ParentClient LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_SYSTEM_VERSION 10.0)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(ParentClient WIN32
    src/main.cpp
    src/config.cpp
    src/device_id.cpp
    src/websocket.cpp
    src/command_handler.cpp
    src/overlay.cpp
    src/tray.cpp
    src/logger.cpp
)

target_include_directories(ParentClient PRIVATE include)
target_link_libraries(ParentClient PRIVATE
    winhttp.lib
    ws2_32.lib
    shell32.lib
    user32.lib
    gdi32.lib
)
