# Firmware package secure download demo.
# Author: GuoYuLong
cmake_minimum_required(VERSION 3.16)

project(firmware_secure_download
  VERSION 1.0.0
  DESCRIPTION "HTTPS firmware downloader with SHA-256 verification"
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(ExternalProject)
include(ProcessorCount)

ProcessorCount(FSD_JOBS)
if(FSD_JOBS EQUAL 0)
  set(FSD_JOBS 1)
endif()

set(FSD_TP_ROOT "${CMAKE_SOURCE_DIR}/3rdparty")
set(FSD_TP_INSTALL "${CMAKE_BINARY_DIR}/3rdparty-install")

foreach(pkg openssl curl)
  if(NOT EXISTS "${FSD_TP_ROOT}/${pkg}/.fsd-source-tag")
    message(FATAL_ERROR
      "missing ${FSD_TP_ROOT}/${pkg}; run 3rdparty/fetch.sh first")
  endif()
endforeach()

set(FSD_OPENSSL_CRYPTO "${FSD_TP_INSTALL}/lib/libcrypto.a")
set(FSD_OPENSSL_SSL "${FSD_TP_INSTALL}/lib/libssl.a")
set(FSD_CURL_LIB "${FSD_TP_INSTALL}/lib/libcurl.a")

# OpenSSL 3.5 没有官方 CMake，安装到构建目录，避免使用本机 libssl。
ExternalProject_Add(openssl_ep
  SOURCE_DIR "${FSD_TP_ROOT}/openssl"
  BINARY_DIR "${CMAKE_BINARY_DIR}/openssl-build"
  INSTALL_DIR "${FSD_TP_INSTALL}"
  DOWNLOAD_COMMAND ""
  UPDATE_COMMAND ""
  CONFIGURE_COMMAND "${FSD_TP_ROOT}/openssl/config"
                    --prefix=${FSD_TP_INSTALL}
                    --libdir=lib
                    no-shared
                    no-tests
                    -fPIC
  BUILD_COMMAND make -j${FSD_JOBS}
  INSTALL_COMMAND make install_sw
  BUILD_BYPRODUCTS "${FSD_OPENSSL_CRYPTO}" "${FSD_OPENSSL_SSL}"
)

# curl 8.23 已去掉 HTTP_ONLY。这里只编静态库，并关掉会再拉第三方的功能。
# 没有 nghttp2 时 HTTPS 仍然可用，协商不到 HTTP/2 就使用 HTTP/1.1。
ExternalProject_Add(curl_ep
  DEPENDS openssl_ep
  SOURCE_DIR "${FSD_TP_ROOT}/curl"
  BINARY_DIR "${CMAKE_BINARY_DIR}/curl-build"
  INSTALL_DIR "${FSD_TP_INSTALL}"
  DOWNLOAD_COMMAND ""
  UPDATE_COMMAND ""
  CMAKE_ARGS
    -DCMAKE_INSTALL_PREFIX=${FSD_TP_INSTALL}
    -DCMAKE_INSTALL_LIBDIR=lib
    -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    -DCMAKE_PREFIX_PATH=${FSD_TP_INSTALL}
    -DOPENSSL_ROOT_DIR=${FSD_TP_INSTALL}
    -DOPENSSL_INCLUDE_DIR=${FSD_TP_INSTALL}/include
    -DOPENSSL_SSL_LIBRARY=${FSD_OPENSSL_SSL}
    -DOPENSSL_CRYPTO_LIBRARY=${FSD_OPENSSL_CRYPTO}
    -DOPENSSL_USE_STATIC_LIBS=ON
    -DBUILD_SHARED_LIBS=OFF
    -DBUILD_STATIC_LIBS=ON
    -DBUILD_CURL_EXE=OFF
    -DBUILD_TESTING=OFF
    -DBUILD_LIBCURL_DOCS=OFF
    -DBUILD_MISC_DOCS=OFF
    -DENABLE_CURL_MANUAL=OFF
    -DCURL_USE_OPENSSL=ON
    -DCURL_USE_LIBPSL=OFF
    -DCURL_USE_LIBSSH2=OFF
    -DCURL_ZLIB=OFF
    -DCURL_BROTLI=OFF
    -DCURL_ZSTD=OFF
    -DUSE_NGHTTP2=OFF
    -DUSE_LIBIDN2=OFF
    -DCURL_DISABLE_LDAP=ON
    -DCURL_DISABLE_LDAPS=ON
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
  BUILD_BYPRODUCTS "${FSD_CURL_LIB}"
)

add_executable(firmware_secure_download
  src/sha256.cc
  src/md5.cc
  src/manifest.cc
  src/http_client.cc
  src/url.cc
  src/secure_downloader.cc
  src/main.cc)

add_dependencies(firmware_secure_download curl_ep)

target_include_directories(firmware_secure_download
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${FSD_TP_INSTALL}/include")

# clangd 用 Clang 解析，并按自己猜测的 GCC 版本找标准库。把当前编译器的
# 实际搜索路径写进编译数据库，跳转才能进入 libstdc++、curl 和 OpenSSL。
execute_process(
  COMMAND ${CMAKE_CXX_COMPILER} -E -x c++ - -v
  INPUT_FILE /dev/null
  OUTPUT_VARIABLE FSD_CXX_VERBOSE_OUT
  ERROR_VARIABLE FSD_CXX_VERBOSE_ERR)
set(FSD_CXX_VERBOSE "${FSD_CXX_VERBOSE_OUT}\n${FSD_CXX_VERBOSE_ERR}")
string(FIND "${FSD_CXX_VERBOSE}" "#include <...> search starts here:" fsd_inc_begin)
string(FIND "${FSD_CXX_VERBOSE}" "End of search list." fsd_inc_end)
if(fsd_inc_begin GREATER -1 AND fsd_inc_end GREATER fsd_inc_begin)
  math(EXPR fsd_inc_len "${fsd_inc_end} - ${fsd_inc_begin}")
  string(SUBSTRING "${FSD_CXX_VERBOSE}" ${fsd_inc_begin} ${fsd_inc_len} fsd_inc_block)
  string(REPLACE "\n" ";" FSD_CXX_SYSTEM_INCLUDES "${fsd_inc_block}")
  foreach(fsd_system_include ${FSD_CXX_SYSTEM_INCLUDES})
    string(STRIP "${fsd_system_include}" fsd_system_include)
    if(fsd_system_include MATCHES "^/" AND IS_DIRECTORY "${fsd_system_include}")
      # 用编译选项而不是 include_directories，避免 CMake 把隐式系统路径从
      # compile_commands.json 里删掉。
      target_compile_options(firmware_secure_download PRIVATE
        "SHELL:-isystem ${fsd_system_include}")
    endif()
  endforeach()
endif()

target_link_libraries(firmware_secure_download
  PRIVATE
    "${FSD_CURL_LIB}"
    "${FSD_OPENSSL_SSL}"
    "${FSD_OPENSSL_CRYPTO}"
    pthread
    dl)

target_compile_options(firmware_secure_download PRIVATE
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

target_compile_definitions(firmware_secure_download PRIVATE
  FSD_VERSION="${PROJECT_VERSION}")

# clangd 从源码根目录向上查找编译数据库，不会自己进入 build/。
if(CMAKE_EXPORT_COMPILE_COMMANDS)
  file(CREATE_LINK
    "${CMAKE_BINARY_DIR}/compile_commands.json"
    "${CMAKE_SOURCE_DIR}/compile_commands.json"
    SYMBOLIC)
endif()
