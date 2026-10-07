cmake_minimum_required(VERSION 3.21)
project(EDSlash VERSION 0.2.0 LANGUAGES C CXX)
# 生产单元与SDL使用同一32位Windows工具链，避免架构和运行库混用。
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4 OR NOT MINGW)
    message(FATAL_ERROR "EDSlash需要32位MinGW工具链；设置EDSLASH_CC或将i686编译器加入PATH。")
endif()
# 入口提供内容摘要；直接CMake构建明确显示未指定身份。
set(EDSLASH_BUILD_ID "未指定源码摘要" CACHE STRING "当前构建输入内容身份")
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
# 一次编译保留完整源码调试信息，优化级别和NDEBUG保持Release原设置。
# 发行件由此链接件剥离符号得到，避免两次编译引入不同的游戏逻辑。
string(APPEND CMAKE_C_FLAGS_RELEASE " -g")
string(APPEND CMAKE_CXX_FLAGS_RELEASE " -g")

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
# 仅用上游官方开关裁剪；手柄、HIDAPI、震动、传感器和公共设施保留。
foreach(feature AUDIO GPU RENDER CAMERA DIALOG TRAY)
    set(SDL_${feature} OFF CACHE BOOL "" FORCE)
endforeach()
foreach(feature JOYSTICK HIDAPI HIDAPI_JOYSTICK HAPTIC SENSOR POWER)
    set(SDL_${feature} ON CACHE BOOL "" FORCE)
endforeach()
# 上游DirectInput检查依赖SDL_DIRECTX，而后者依赖VIDEO/AUDIO配置。
# 保留Windows Video公共代码以恢复DirectInput设备支持；实际运行仍只初始化GAMEPAD。
set(SDL_VIDEO ON CACHE BOOL "" FORCE)
set(SDL_DIRECTX ON CACHE BOOL "" FORCE)
set(SDL_OPENGL OFF CACHE BOOL "" FORCE)
set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)
set(SDL_VULKAN OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/SDL-release-3.4.16"
                 "${CMAKE_CURRENT_BINARY_DIR}/SDL" EXCLUDE_FROM_ALL)
# GCC16的IPA克隆可能丢失HID单元内被函数表引用的静态包装函数。
# 仅该翻译单元关闭克隆；仍用完整上游CMake目标，源码及设备后端不改。
if(CMAKE_C_COMPILER_ID STREQUAL "GNU" AND CMAKE_C_COMPILER_VERSION VERSION_GREATER_EQUAL 16
   AND CMAKE_C_COMPILER_VERSION VERSION_LESS 17)
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/SDL-release-3.4.16/src/hidapi/SDL_hidapi.c"
        DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../thirdparty/SDL-release-3.4.16"
        PROPERTIES COMPILE_OPTIONS "-fno-ipa-cp-clone")
endif()
file(GLOB_RECURSE EDSLASH_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
add_library(EDSlash SHARED ${EDSLASH_SOURCES})
set_target_properties(EDSlash PROPERTIES PREFIX "" SUFFIX ".asi" OUTPUT_NAME "EDSlash")
target_include_directories(EDSlash PRIVATE src)
target_compile_options(EDSlash PRIVATE -O2 -Wall -Wextra -Werror -fms-extensions -finput-charset=UTF-8 -fexec-charset=UTF-8)
target_link_libraries(EDSlash PRIVATE SDL3::SDL3-static user32 advapi32 m)
# 编译器运行库静态链接；系统DLL由Windows提供，不再需要外置SDL3.dll。
target_link_options(EDSlash PRIVATE -static -static-libgcc -static-libstdc++ -Wl,--exclude-all-symbols -Wl,--dynamicbase -Wl,--nxcompat)
target_compile_definitions(EDSlash PRIVATE SDL_STATIC_LIB EDSLASH_BUILD_ID="${EDSLASH_BUILD_ID}")
enable_testing()
function(edslash_test name)
    add_executable(${name} ${ARGN})
    target_include_directories(${name} PRIVATE src/Modules/Controller src)
    target_compile_options(${name} PRIVATE -O2 -Wall -Wextra -Werror -finput-charset=UTF-8 -fexec-charset=UTF-8)
    target_link_options(${name} PRIVATE -static -static-libgcc)
    add_test(NAME ${name} COMMAND ${name})
endfunction()
edslash_test(test_crash tools/controller/test_crash.c)
edslash_test(test_control src/Modules/Controller/Control.c tools/controller/test_control.c)
target_link_libraries(test_control PRIVATE m)
edslash_test(test_motion src/Modules/Controller/Control.c tools/controller/test_motion.c)
target_link_libraries(test_motion PRIVATE m)
edslash_test(test_game src/Modules/Controller/Control.c src/Modules/Controller/Game.c
    src/Modules/Controller/Combat.c src/Modules/Controller/SkillResolver.c
    src/Modules/Controller/Guard.c src/Modules/Controller/Feedback.c
    src/Modules/Controller/ActionMenu.c src/Modules/Controller/Menu.c src/Modules/Controller/Inspect.c src/Runtime/HookManager.c tools/controller/test_game.c)
target_link_libraries(test_game PRIVATE user32 m)
# 菜单回放同时调用真实Control/Menu/Game，验证虚表清栈和关闭后不泄漏战斗输入。
edslash_test(test_menu src/Modules/Controller/Control.c src/Modules/Controller/Game.c
    src/Modules/Controller/Combat.c src/Modules/Controller/SkillResolver.c
    src/Modules/Controller/Guard.c src/Modules/Controller/Feedback.c
    src/Modules/Controller/ActionMenu.c src/Modules/Controller/Menu.c src/Modules/Controller/Inspect.c src/Modules/Controller/Cursor.c src/Modules/Controller/Crash.c src/Runtime/FileIO.c src/Runtime/HookManager.c tools/controller/test_menu.c)
target_link_libraries(test_menu PRIVATE user32 m)
add_test(NAME test_focus_containment COMMAND test_menu --containment)
add_test(NAME test_skill_visual_navigation COMMAND test_menu --skill-focus navigation)
add_test(NAME test_skill_action_frame COMMAND test_menu --skill-focus frame)
add_test(NAME test_skill_y_focus COMMAND test_menu --skill-focus preserve)
add_test(NAME test_skill_default_focus COMMAND test_menu --skill-focus default)
add_test(NAME test_jump COMMAND test_game --jump)
add_test(NAME test_jump_zero_distance COMMAND test_game --jump-zero)
# 调查回放使用真实地图格/句柄与原事件包装，不读取游戏进程或真实资源脚本。
edslash_test(test_inspect src/Modules/Controller/Control.c src/Modules/Controller/Game.c
    src/Modules/Controller/Combat.c src/Modules/Controller/SkillResolver.c
    src/Modules/Controller/Guard.c src/Modules/Controller/Feedback.c
    src/Modules/Controller/ActionMenu.c src/Modules/Controller/Menu.c src/Modules/Controller/Inspect.c src/Runtime/HookManager.c tools/controller/test_inspect.c)
target_link_libraries(test_inspect PRIVATE user32 m)
# 设置回归调用生产同步入口、Guard及真实配置保存，补足原先两个单元间的断点。
edslash_test(test_guard_settings src/Modules/Controller/Control.c src/Modules/Controller/Game.c
    src/Modules/Controller/Combat.c src/Modules/Controller/SkillResolver.c
    src/Modules/Controller/Guard.c src/Modules/Controller/Feedback.c
    src/Modules/Controller/ActionMenu.c src/Modules/Controller/Menu.c src/Modules/Controller/Inspect.c src/Runtime/HookManager.c
    src/Runtime/Config.c src/Runtime/Toml.c src/Runtime/FileIO.c tools/controller/test_guard_settings.c)
target_link_libraries(test_guard_settings PRIVATE user32 m)

# 启动故障回归链接生产Controller业务、Registry和配置，游戏地址仅使用宿主数组。
edslash_test(test_startup tools/controller/test_startup.c src/Runtime/ModuleRegistry.c
    src/Runtime/HookManager.c src/Runtime/EventBus.c src/Runtime/Config.c src/Runtime/Toml.c src/Runtime/FileIO.c
    src/Modules/Controller/Control.c src/Modules/Controller/Game.c src/Modules/Controller/Combat.c
    src/Modules/Controller/SkillResolver.c src/Modules/Controller/Guard.c src/Modules/Controller/Feedback.c
    src/Modules/Controller/ActionMenu.c src/Modules/Controller/Menu.c src/Modules/Controller/Cursor.c src/Modules/Controller/Crash.c src/Modules/Controller/Inspect.c src/Runtime/Focus.c)
target_link_libraries(test_startup PRIVATE user32 m)

edslash_test(test_notice tools/qol/test_notice.c)
edslash_test(test_autopickup tools/qol/test_autopickup.c)
edslash_test(test_runtime tools/test_runtime.c src/Runtime/X86Detour.c)
# 探针链接真实管理器和事件总线，纳入正式回归。
edslash_test(audit_runtime_contracts tools/audit_runtime_contracts.c src/Runtime/HookManager.c src/Runtime/EventBus.c)
edslash_test(test_input_frame tools/test_input_frame.c)
edslash_test(test_log tools/test_log.c src/Runtime/Log.c src/Runtime/FileIO.c)
target_compile_definitions(test_log PRIVATE EDSLASH_BUILD_ID="${EDSLASH_BUILD_ID}")
edslash_test(test_config tools/test_config.c src/Runtime/Config.c src/Runtime/Toml.c src/Runtime/FileIO.c)
target_compile_options(test_config PRIVATE -municode)
target_link_options(test_config PRIVATE -municode)
edslash_test(test_sdl tools/controller/test_sdl.c src/Modules/Controller/Input.c
    src/Modules/Controller/Control.c src/Runtime/Config.c src/Runtime/Toml.c src/Runtime/FileIO.c)
target_compile_definitions(test_sdl PRIVATE SDL_STATIC_LIB)
target_link_libraries(test_sdl PRIVATE SDL3::SDL3-static user32 m)
target_link_options(test_sdl PRIVATE -static-libstdc++)
add_executable(config_template tools/config_template.c src/Runtime/Config.c src/Runtime/Toml.c src/Runtime/FileIO.c)
target_compile_options(config_template PRIVATE -municode -Wall -Wextra -Werror -finput-charset=UTF-8 -fexec-charset=UTF-8)
target_link_options(config_template PRIVATE -municode -static -static-libgcc)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/EDSlash.toml"
    COMMAND config_template "${CMAKE_CURRENT_BINARY_DIR}/EDSlash.toml"
    DEPENDS config_template VERBATIM)
add_custom_target(default_config ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/EDSlash.toml")
add_executable(test_load tools/controller/test_load.c)
target_compile_options(test_load PRIVATE -municode -Wall -Wextra -Werror)
target_link_options(test_load PRIVATE -municode -static -static-libgcc)
add_test(NAME unified_asi_load COMMAND test_load "$<TARGET_FILE:EDSlash>")
