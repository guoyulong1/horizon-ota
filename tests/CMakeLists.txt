# 单元测试：GoogleTest + ctest。
# - googletest 源码由根目录 3rdparty/fetch.sh 按固定标签拉取（校验 SHA-256），
#   不入库；gtest_discover_tests 把每个 TEST 用例注册成独立的 ctest 条目；
# - fsd_self_test 保留 fsd 命令行自检这条底线；
# - tests/fsd/ 是 fsd 模块的移植测试，守护 monorepo 内的 vendored 副本。

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/3rdparty/googletest/.ota-source-tag")
  message(FATAL_ERROR
    "missing 3rdparty/googletest; run 3rdparty/fetch.sh first")
endif()

# 只编测试需要的部分：不装 gtest、不编它自带的示例与自测。
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(gtest_build_tests OFF CACHE BOOL "" FORCE)
set(gtest_build_samples OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

# gtest_discover_tests 来自 CMake 自带的 GoogleTest 模块。
include(GoogleTest)

add_subdirectory("${CMAKE_SOURCE_DIR}/3rdparty/googletest"
                 "${CMAKE_BINARY_DIR}/googletest-build"
                 EXCLUDE_FROM_ALL)

# 单元测试统一挂 -Wall -Wextra -Wpedantic（原有级别；不含模块那组
# 严格开关，避免和 gtest 头文件互相纠缠）。
function(ota_add_gtest_test name)
  add_executable(test_${name} ${ARGN})
  target_link_libraries(test_${name} PRIVATE GTest::gtest_main)
  target_include_directories(test_${name}
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_compile_options(test_${name} PRIVATE -Wall -Wextra -Wpedantic)
  gtest_discover_tests(test_${name} DISCOVERY_TIMEOUT 120)
endfunction()

# pack：发布端加密（系统 OpenSSL），按模块独立性不链接 fsd。
function(ota_add_pack_test name)
  ota_add_gtest_test(${name} ${ARGN})
  target_link_libraries(test_${name} PRIVATE ota_pack_crypto)
endfunction()

# verify：设备端验签与代理核心（传递链接 fsd 与 nlohmann）。
function(ota_add_verify_test name)
  ota_add_gtest_test(${name} ${ARGN})
  target_link_libraries(test_${name} PRIVATE ota_verify_core)
endfunction()

# fsd：设备端下载模块（monorepo vendored 副本，与独立 fsd 仓库同构）。
function(ota_add_fsd_test name)
  ota_add_gtest_test(${name} ${ARGN})
  target_link_libraries(test_${name} PRIVATE fsd)
endfunction()

ota_add_pack_test(pack_crypto unit/test_pack_crypto.cc)
ota_add_verify_test(verify_crypto unit/test_verify_crypto.cc)
ota_add_verify_test(verify_manifest unit/test_verify_manifest.cc)
ota_add_verify_test(verify_config unit/test_verify_config.cc)
ota_add_verify_test(verify_state unit/test_verify_state.cc)
ota_add_verify_test(verify_agent unit/test_verify_agent.cc)

ota_add_fsd_test(fsd_text_util fsd/test_text_util.cc)
ota_add_fsd_test(fsd_status fsd/test_status.cc)
ota_add_fsd_test(fsd_url fsd/test_url.cc)
ota_add_fsd_test(fsd_sha256 fsd/test_sha256.cc)
ota_add_fsd_test(fsd_md5 fsd/test_md5.cc)
ota_add_fsd_test(fsd_manifest fsd/test_manifest.cc)
ota_add_fsd_test(fsd_http_client fsd/test_http_client.cc)
ota_add_fsd_test(fsd_secure_downloader fsd/test_secure_downloader.cc)

# fsd 命令行自检也纳入 ctest 编排，保持「--self-test 退出码 0」这条底线。
add_test(NAME fsd_self_test
         COMMAND firmware_secure_download --self-test)
set_tests_properties(fsd_self_test PROPERTIES TIMEOUT 120)
