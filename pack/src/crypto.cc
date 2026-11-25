// 发布端通用加密工具实现：只使用 OpenSSL EVP，不依赖 fsd。
#include "crypto.h"

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>

#include <array>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace ota {
namespace {

std::string ToHex(const unsigned char* data, std::size_t len) {
  static const char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(len * 2);
  for (std::size_t i = 0; i < len; ++i) {
    out.push_back(kHex[data[i] >> 4]);
    out.push_back(kHex[data[i] & 0x0f]);
  }
  return out;
}

[[noreturn]] void ThrowOpenSsl(const std::string& what) {
  const unsigned long err = ERR_get_error();
  std::array<char, 256> buffer{};
  if (err != 0) {
    ERR_error_string_n(err, buffer.data(), buffer.size());
  }
  throw std::runtime_error(
      what + ": " + (err != 0 ? buffer.data() : "unknown OpenSSL error"));
}

using EvpMdCtx = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
using EvpPkey = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;

}  // namespace

std::string Sha256Hex(const std::string& data) {
  EvpMdCtx ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  if (!ctx) {
    ThrowOpenSsl("EVP_MD_CTX_new");
  }
  if (EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1) {
    ThrowOpenSsl("EVP_DigestInit_ex");
  }
  if (EVP_DigestUpdate(ctx.get(), data.data(), data.size()) != 1) {
    ThrowOpenSsl("EVP_DigestUpdate");
  }
  unsigned char md[EVP_MAX_MD_SIZE];
  unsigned int md_len = 0;
  if (EVP_DigestFinal_ex(ctx.get(), md, &md_len) != 1) {
    ThrowOpenSsl("EVP_DigestFinal_ex");
  }
  return ToHex(md, md_len);
}

std::string Sha256FileHex(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("cannot open " + path);
  }
  EvpMdCtx ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  if (!ctx) {
    ThrowOpenSsl("EVP_MD_CTX_new");
  }
  if (EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1) {
    ThrowOpenSsl("EVP_DigestInit_ex");
  }
  std::vector<char> buffer(64 * 1024);
  while (in) {
    in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize got = in.gcount();
    if (got > 0 &&
        EVP_DigestUpdate(ctx.get(), buffer.data(),
                         static_cast<std::size_t>(got)) != 1) {
      ThrowOpenSsl("EVP_DigestUpdate");
    }
  }
  if (in.bad()) {
    throw std::runtime_error("read error on " + path);
  }
  unsigned char md[EVP_MAX_MD_SIZE];
  unsigned int md_len = 0;
  if (EVP_DigestFinal_ex(ctx.get(), md, &md_len) != 1) {
    ThrowOpenSsl("EVP_DigestFinal_ex");
  }
  return ToHex(md, md_len);
}

std::string SignEd25519(const std::string& private_key_pem,
                        const std::string& data) {
  BIO* bio = BIO_new_mem_buf(private_key_pem.data(),
                             static_cast<int>(private_key_pem.size()));
  if (bio == nullptr) {
    ThrowOpenSsl("BIO_new_mem_buf");
  }
  EvpPkey key(PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr),
              EVP_PKEY_free);
  BIO_free(bio);
  if (!key) {
    ThrowOpenSsl("PEM_read_bio_PrivateKey");
  }

  EvpMdCtx ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
  if (!ctx) {
    ThrowOpenSsl("EVP_MD_CTX_new");
  }
  // Ed25519 不经过摘要算法，摘要参数必须为 nullptr；且只能一次喂完。
  if (EVP_DigestSignInit(ctx.get(), nullptr, nullptr, nullptr, key.get()) !=
      1) {
    ThrowOpenSsl("EVP_DigestSignInit");
  }
  std::size_t sig_len = 0;
  const auto* message = reinterpret_cast<const unsigned char*>(data.data());
  if (EVP_DigestSign(ctx.get(), nullptr, &sig_len, message, data.size()) != 1) {
    ThrowOpenSsl("EVP_DigestSign(size)");
  }
  std::string signature(sig_len, '\0');
  if (EVP_DigestSign(ctx.get(), reinterpret_cast<unsigned char*>(signature.data()),
                     &sig_len, message, data.size()) != 1) {
    ThrowOpenSsl("EVP_DigestSign");
  }
  signature.resize(sig_len);
  return signature;
}

std::string SignEd25519File(const std::string& private_key_path,
                            const std::string& data) {
  std::ifstream in(private_key_path, std::ios::binary);
  if (!in) {
    throw std::runtime_error("cannot open private key: " + private_key_path);
  }
  const std::string pem((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
  return SignEd25519(pem, data);
}

}  // namespace ota
