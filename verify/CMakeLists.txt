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

add_executable(ota-fetch
  src/ota_fetch.cc
  src/crypto.cc)

target_include_directories(ota-fetch PRIVATE
  src
  "${OTA_VENDOR_DIR}")

target_link_libraries(ota-fetch PRIVATE fsd)
target_compile_options(ota-fetch PRIVATE ${OTA_WARNINGS})
