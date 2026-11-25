cmake_minimum_required(VERSION 3.16)
project(ota_pack VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# 发布端只依赖通用库：OpenSSL（EVP SHA-256 / Ed25519）与 nlohmann/json。
# 不依赖 fsd —— pack 最终部署在服务器/CI，fsd 部署在设备，二者独立。
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# nlohmann/json 单头：include 目录应指向包含 nlohmann/ 子树的父目录，
# 这样 <nlohmann/json.hpp> 才能解析。默认取 monorepo 的 3rdparty。
set(OTA_NLOHMANN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../3rdparty"
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

add_library(ota_pack_crypto STATIC src/crypto.cc)
target_include_directories(ota_pack_crypto PUBLIC src)
target_link_libraries(ota_pack_crypto PUBLIC OpenSSL::Crypto Threads::Threads ${CMAKE_DL_LIBS})
target_compile_options(ota_pack_crypto PRIVATE ${OTA_WARNINGS})

add_executable(ota-pack src/ota_pack.cc)
target_include_directories(ota-pack PRIVATE src "${OTA_NLOHMANN_DIR}")
target_link_libraries(ota-pack PRIVATE ota_pack_crypto)
target_compile_options(ota-pack PRIVATE ${OTA_WARNINGS})

add_executable(ota-sign src/ota_sign.cc)
target_include_directories(ota-sign PRIVATE src)
target_link_libraries(ota-sign PRIVATE ota_pack_crypto)
target_compile_options(ota-sign PRIVATE ${OTA_WARNINGS})
