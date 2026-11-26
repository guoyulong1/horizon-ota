# 设备端 OTA 代理：状态机与编排（docs/OTA.md 1.3）。
# agent 是唯一同时链接 fsd（安全传输）与 ota_verify（验签 + 适用性）的模块；
# 自身不做密码学判断。边界见 docs/adr/0001。
# 依赖 fsd/verify 的 CMake 目标，随 monorepo 顶层构建（./scripts/verify.sh）。

# 防误用：agent 不能脱离 monorepo 单独配置（fsd/verify 目标由顶层提供）。
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  message(FATAL_ERROR
    "agent must be built from the monorepo root (cmake -S . -B build); "
    "verify can be built standalone: cmake -S verify -B build")
endif()

set(OTA_WARNINGS
  -Wall
  -Wextra
  -Wpedantic
  -Wshadow
  -Wconversion
  -Wsign-conversion
  -Wnon-virtual-dtor
  -Wold-style-cast
  -Woverloaded-virtual
  -Wnull-dereference
  -Wdouble-promotion
  -Wformat=2
  -Wimplicit-fallthrough)

# agent 自带 3rdparty（nlohmann，配置/状态 JSON 用），随模块部署。
set(OTA_NLOHMANN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty"
    CACHE PATH "directory containing the nlohmann/ subtree")

# 代理核心：配置、取文件、下载编排、状态持久化、状态机。
add_library(ota_agent_core STATIC
  src/config.cc
  src/fetch.cc
  src/download.cc
  src/state.cc
  src/agent.cc)
target_include_directories(ota_agent_core PUBLIC src "${OTA_NLOHMANN_DIR}")
target_link_libraries(ota_agent_core PUBLIC ota_verify fsd)
target_compile_options(ota_agent_core PRIVATE ${OTA_WARNINGS})

# 命令行入口：链接代理核心。
add_executable(ota-agent src/ota_agent.cc)
target_link_libraries(ota-agent PRIVATE ota_agent_core)
target_compile_options(ota-agent PRIVATE ${OTA_WARNINGS})
