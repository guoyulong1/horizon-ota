# 设备端验签库：发布清单验签（Ed25519）+ 适用性判断。
# 纯函数、无状态、无网络/下载/状态编排，不依赖 fsd API。
# - monorepo 聚合构建：经 fsd_openssl 接口目标复用 fsd 构建的同一份 OpenSSL；
# - 单独构建（像 fsd 一样）：cmake -S verify -B build，用系统 OpenSSL。
# 边界见 docs/adr/0001。
cmake_minimum_required(VERSION 3.16)
project(ota_verify VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# verify 自带 3rdparty（nlohmann），随模块部署。
set(OTA_NLOHMANN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty"
    CACHE PATH "directory containing the nlohmann/ subtree")

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

# 验签库：验签（crypto）与清单解析/适用性（manifest），纯函数。
add_library(ota_verify STATIC
  src/crypto.cc
  src/manifest.cc)
target_include_directories(ota_verify PUBLIC src "${OTA_NLOHMANN_DIR}")
if(TARGET fsd_openssl)
  target_link_libraries(ota_verify PUBLIC fsd_openssl)
else()
  find_package(OpenSSL REQUIRED)
  target_link_libraries(ota_verify PUBLIC OpenSSL::Crypto)
endif()
target_compile_options(ota_verify PRIVATE ${OTA_WARNINGS})

# 命令行入口（与 fsd 同构：库 + 可执行程序）：
#   ota-verify --self-test   内置自检（ctest 底线 verify_self_test）
#   ota-verify check ...     手动验一份清单的签名
add_executable(ota-verify src/ota_verify.cc)
target_link_libraries(ota-verify PRIVATE ota_verify)
target_compile_options(ota-verify PRIVATE ${OTA_WARNINGS})
