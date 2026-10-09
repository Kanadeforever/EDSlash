cmake_minimum_required(VERSION 3.21)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
project(EDSlash VERSION 0.2.0 LANGUAGES C CXX)
# 关于页读取项目唯一版本定义，不另抄一份版本号。
add_compile_definitions(EDSLASH_VERSION=\"${PROJECT_VERSION}\")
# 生产单元与SDL使用同一32位Windows工具链，避免架构和运行库混用。
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4 OR NOT CMAKE_C_COMPILER_ID STREQUAL "MSVC")
    message(FATAL_ERROR "EDSlash需要32位MSVC工具链；安装桌面C++、x86工具和Windows SDK。")
endif()
# 入口提供内容摘要；直接CMake构建明确显示未指定身份。
set(EDSLASH_BUILD_ID "未指定源码摘要" CACHE STRING "当前构建输入内容身份")
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
# 一次编译保留完整源码调试信息，优化级别和NDEBUG保持Release原设置。
# 发行与调试ASI复制同一链接件，符号放在外置PDB，避免重复编译造成逻辑差异。
string(APPEND CMAKE_C_FLAGS_RELEASE " /Zi")
string(APPEND CMAKE_CXX_FLAGS_RELEASE " /Zi")

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
file(GLOB_RECURSE EDSLASH_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
add_library(EDSlash SHARED ${EDSLASH_SOURCES} src/EDSlash.def)
set_target_properties(EDSlash PROPERTIES PREFIX "" SUFFIX ".asi" OUTPUT_NAME "EDSlash")
target_include_directories(EDSlash PRIVATE src)
# 地址桥需要函数/数据指针转换；聚合初始化和已有收窄规则沿用Win32数据模型。
# 其余/W4诊断仍由/WX阻止交付，变量遮蔽与未初始化问题在源码中修正。
target_compile_options(EDSlash PRIVATE /O2 /W4 /WX /utf-8 /wd4204 /wd4100 /wd4244 /wd4267 /wd4055 /wd4152)
target_link_libraries(EDSlash PRIVATE SDL3::SDL3-static user32 advapi32)
# 编译器运行库静态链接；系统DLL由Windows提供，不再需要外置SDL3.dll。
target_link_options(EDSlash PRIVATE /DEBUG:FULL /INCREMENTAL:NO /OPT:REF /OPT:NOICF /DYNAMICBASE /NXCOMPAT /PDBALTPATH:EDSlash.pdb)
# 运行库使用/MT；源码仍为C17，中文字符串按UTF-8编译。
target_compile_definitions(EDSlash PRIVATE _CRT_SECURE_NO_WARNINGS)
set_target_properties(EDSlash PROPERTIES PDB_NAME "EDSlash" COMPILE_PDB_NAME "EDSlash_compile")
target_compile_definitions(EDSlash PRIVATE SDL_STATIC_LIB EDSLASH_BUILD_ID="${EDSLASH_BUILD_ID}")
enable_testing()
function(edslash_test name)
    add_executable(${name} ${ARGN} src/Runtime/RuntimeText.c
        src/Modules/Controller/ControllerText.c src/Modules/QOL/QOLText.c
        src/Modules/DisplayFix/DisplayFixText.c)
    target_include_directories(${name} PRIVATE src/Modules/Controller src)
    target_compile_options(${name} PRIVATE /O2 /W4 /WX /utf-8 /wd4204 /wd4100 /wd4244 /wd4267 /wd4055 /wd4152)
    target_link_options(${name} PRIVATE /INCREMENTAL:NO /OPT:NOICF)
    target_compile_definitions(${name} PRIVATE _CRT_SECURE_NO_WARNINGS)
    add_test(NAME ${name} COMMAND ${name})
endfunction()
edslash_test(test_crash tools/controller/test_crash.c)
edslash_test(test_control src/Modules/Controller/Control.c tools/controller/test_control.c)
edslash_test(test_motion src/Modules/Controller/Control.c tools/controller/test_motion.c)
edslash_test(test_game src/Modules/Controller/Control.c src/Modules/Controller/Game.c
    src/Modules/Controller/Combat.c src/Modules/Controller/SkillResolver.c
    src/Modules/Controller/Guard.c src/Modules/Controller/Feedback.c
    src/Modules/Controller/ActionMenu.c src/Modules/Controller/Menu.c src/Modules/Controller/Inspect.c src/Runtime/HookManager.c tools/controller/test_game.c)
target_link_libraries(test_game PRIVATE user32)
# 菜单回放同时调用真实Control/Menu/Game，验证虚表清栈和关闭后不泄漏战斗输入。
edslash_test(test_menu src/Modules/Controller/Control.c src/Modules/Controller/Game.c
    src/Modules/Controller/Combat.c src/Modules/Controller/SkillResolver.c
    src/Modules/Controller/Guard.c src/Modules/Controller/Feedback.c
    src/Modules/Controller/ActionMenu.c src/Modules/Controller/Menu.c src/Modules/Controller/Inspect.c src/Modules/Controller/Cursor.c src/Modules/Controller/Crash.c src/Runtime/FileIO.c src/Runtime/HookManager.c tools/controller/test_menu.c)
target_link_libraries(test_menu PRIVATE user32)
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
target_link_libraries(test_inspect PRIVATE user32)
# 设置回归调用生产同步入口、Guard及真实配置保存，补足原先两个单元间的断点。
edslash_test(test_guard_settings src/Modules/Controller/Control.c src/Modules/Controller/Game.c
    src/Modules/Controller/Combat.c src/Modules/Controller/SkillResolver.c
    src/Modules/Controller/Guard.c src/Modules/Controller/Feedback.c
    src/Modules/Controller/ActionMenu.c src/Modules/Controller/Menu.c src/Modules/Controller/Inspect.c src/Runtime/HookManager.c
    src/Runtime/Config.c src/Runtime/Toml.c src/Runtime/FileIO.c tools/controller/test_guard_settings.c)
target_link_libraries(test_guard_settings PRIVATE user32)

# 启动故障回归链接生产Controller业务、Registry和配置，游戏地址仅使用宿主数组。
edslash_test(test_startup tools/controller/test_startup.c src/Runtime/ModuleRegistry.c
    src/Runtime/HookManager.c src/Runtime/EventBus.c src/Runtime/Config.c src/Runtime/Toml.c src/Runtime/FileIO.c
    src/Modules/Controller/Control.c src/Modules/Controller/Game.c src/Modules/Controller/Combat.c
    src/Modules/Controller/SkillResolver.c src/Modules/Controller/Guard.c src/Modules/Controller/Feedback.c
    src/Modules/Controller/ActionMenu.c src/Modules/Controller/Menu.c src/Modules/Controller/Cursor.c src/Modules/Controller/Crash.c src/Modules/Controller/Inspect.c src/Runtime/Focus.c)
target_sources(test_startup PRIVATE src/Runtime/SettingsWindow.c src/Runtime/SettingsModel.c)
target_link_libraries(test_startup PRIVATE user32 gdi32)

edslash_test(test_notice tools/qol/test_notice.c)
edslash_test(test_autopickup tools/qol/test_autopickup.c)
edslash_test(test_random_name_ui tools/qol/test_random_name_ui.c src/Modules/QOL/RandomName.c)
target_link_libraries(test_random_name_ui PRIVATE user32 gdi32)
edslash_test(test_runtime tools/test_runtime.c src/Runtime/X86Detour.c)
# 探针链接真实管理器和事件总线，纳入正式回归。
edslash_test(audit_runtime_contracts tools/audit_runtime_contracts.c src/Runtime/HookManager.c src/Runtime/EventBus.c)
edslash_test(test_input_frame tools/test_input_frame.c)
edslash_test(test_log tools/test_log.c src/Runtime/Log.c src/Runtime/FileIO.c)
target_compile_definitions(test_log PRIVATE EDSLASH_BUILD_ID="${EDSLASH_BUILD_ID}")
# 独立回放英文ANSI、简体中文和UTF-8系统代码页，不要求CI修改系统区域。
# 每个可执行文件有独立目录，避免并行CTest写入同一个EDSlash.log。
foreach(codepage 1252 936 65001)
    edslash_test(test_log_acp${codepage} tools/test_log.c tools/test_log_codepage.c
        src/Runtime/Log.c src/Runtime/FileIO.c)
    target_compile_options(test_log_acp${codepage} PRIVATE
        "/FI${CMAKE_CURRENT_SOURCE_DIR}/tools/test_log_codepage.h")
    target_compile_definitions(test_log_acp${codepage} PRIVATE
        EDSLASH_TEST_ACP=${codepage} EDSLASH_BUILD_ID="${EDSLASH_BUILD_ID}")
    set_target_properties(test_log_acp${codepage} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/log-codepage-${codepage}")
endforeach()
edslash_test(test_config tools/test_config.c src/Runtime/SettingsModel.c src/Runtime/Config.c src/Runtime/Toml.c src/Runtime/FileIO.c)
edslash_test(test_settings_window tools/test_settings_window.c src/Runtime/SettingsModel.c src/Runtime/Config.c src/Runtime/Toml.c src/Runtime/FileIO.c)
target_compile_definitions(test_settings_window PRIVATE EDSLASH_BUILD_ID=\"${EDSLASH_BUILD_ID}\")
target_link_libraries(test_settings_window PRIVATE user32 gdi32)
edslash_test(test_sdl tools/controller/test_sdl.c src/Modules/Controller/Input.c
    src/Modules/Controller/Control.c src/Runtime/Config.c src/Runtime/Toml.c src/Runtime/FileIO.c)
target_compile_definitions(test_sdl PRIVATE SDL_STATIC_LIB)
target_link_libraries(test_sdl PRIVATE SDL3::SDL3-static user32)
add_executable(config_template src/Runtime/RuntimeText.c tools/config_template.c src/Runtime/Config.c src/Runtime/Toml.c src/Runtime/FileIO.c)
target_compile_options(config_template PRIVATE /W4 /WX /utf-8 /wd4204 /wd4100 /wd4244 /wd4267 /wd4055 /wd4152)
target_link_options(config_template PRIVATE /INCREMENTAL:NO /OPT:NOICF)
target_compile_definitions(config_template PRIVATE _CRT_SECURE_NO_WARNINGS)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/EDSlash.toml"
    COMMAND config_template "${CMAKE_CURRENT_BINARY_DIR}/EDSlash.toml"
    DEPENDS config_template VERBATIM)
add_custom_target(default_config ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/EDSlash.toml")
add_executable(test_load tools/controller/test_load.c)
target_compile_options(test_load PRIVATE /W4 /WX /utf-8)
target_link_options(test_load PRIVATE /INCREMENTAL:NO /OPT:NOICF)
add_test(NAME unified_asi_load COMMAND test_load "$<TARGET_FILE:EDSlash>")
