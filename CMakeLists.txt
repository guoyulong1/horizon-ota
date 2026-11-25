# horizon-ota 顶层构建：设备端（fsd + verify）与开发期聚合。
#
# - `pack/` 有独立的 CMake，可单独构建/部署到服务器（不依赖 fsd）；
#   这里也纳入聚合构建，方便开发。
# - `verify/` 链接 fsd 静态库，同属设备端。
# - 纯发布端部署时，只构建 `pack/`：`cmake -S pack -B build-pack`。
cmake_minimum_required(VERSION 3.16)
project(horizon_ota LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# 共享 vendored 依赖目录（nlohmann/json）。
set(OTA_VENDOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty"
    CACHE PATH "vendored dependencies")

add_subdirectory(fsd)
add_subdirectory(verify)
add_subdirectory(pack)
