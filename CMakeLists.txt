# horizon-ota 顶层构建：设备端（fsd + verify + agent）与开发期聚合。
#
# - `pack/` 有独立的 CMake，可单独构建/部署到服务器（不依赖 fsd）；
#   这里也纳入聚合构建，方便开发。
# - 设备端分层：`fsd/` 安全传输；`verify/` 验签 + 适用性（纯库，可单独构建）；
#   `agent/` OTA 代理，唯一同时链接 fsd 与 verify 的编排模块（docs/adr/0001）。
# - 纯发布端部署时，只构建 `pack/`：`cmake -S pack -B build-pack`。
cmake_minimum_required(VERSION 3.16)
project(horizon_ota LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# 聚合构建把可执行文件统一放到 build/bin/，便于测试脚本定位。
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_subdirectory(fsd)
add_subdirectory(verify)
add_subdirectory(agent)
add_subdirectory(pack)

# 单元测试：GoogleTest + ctest。googletest 源码由根目录 3rdparty/fetch.sh
# 按固定标签拉取；-DBUILD_TESTING=OFF 可整体关掉（设备端仍需 fsd 的
# 3rdparty 才能构建）。
option(BUILD_TESTING "Build unit tests (GoogleTest)" ON)
if(BUILD_TESTING)
  enable_testing()
  add_subdirectory(tests)
endif()
