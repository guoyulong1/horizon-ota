# CTest 单元测试。
# - test_pack_crypto：pack 的 SHA-256（OpenSSL 系统版）。
# - test_verify_crypto：verify 的公钥解析 + Ed25519 验签（与 fsd 同一 OpenSSL）。
# - fsd_self_test：fsd 自检（SHA-256/MD5 向量、清单解析）。
add_executable(test_pack_crypto unit/test_pack_crypto.cc)
target_link_libraries(test_pack_crypto PRIVATE ota_pack_crypto)
target_compile_options(test_pack_crypto PRIVATE -Wall -Wextra -Wpedantic)

add_executable(test_verify_crypto unit/test_verify_crypto.cc)
target_link_libraries(test_verify_crypto PRIVATE ota_verify_crypto)
target_compile_options(test_verify_crypto PRIVATE -Wall -Wextra -Wpedantic)

add_test(NAME pack_crypto COMMAND test_pack_crypto)
add_test(NAME verify_crypto COMMAND test_verify_crypto)
add_test(NAME fsd_self_test COMMAND firmware_secure_download --self-test)
