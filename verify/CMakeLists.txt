# 设备端工具 ota-fetch：链接 fsd 静态库（同属设备端构建），不依赖 pack。
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

# verify 自带 3rdparty（nlohmann），随模块部署。
set(OTA_NLOHMANN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty"
    CACHE PATH "directory containing the nlohmann/ subtree")

# 验签库：与 fsd 使用同一个 OpenSSL（经 fsd 的 PUBLIC 链接传递），
# 供 ota-fetch 与单元测试复用。
add_library(ota_verify_crypto STATIC src/crypto.cc)
target_include_directories(ota_verify_crypto PUBLIC src)
target_link_libraries(ota_verify_crypto PUBLIC fsd)
target_compile_options(ota_verify_crypto PRIVATE ${OTA_WARNINGS})

add_executable(ota-fetch src/ota_fetch.cc)
target_include_directories(ota-fetch PRIVATE src "${OTA_NLOHMANN_DIR}")
target_link_libraries(ota-fetch PRIVATE ota_verify_crypto fsd)
target_compile_options(ota-fetch PRIVATE ${OTA_WARNINGS})
