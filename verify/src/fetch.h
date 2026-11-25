// 用 fsd::HttpClient 取小文件（发布清单/签名/通道指针），不要求摘要。
#ifndef OTA_VERIFY_FETCH_H_
#define OTA_VERIFY_FETCH_H_

#include <string>

namespace ota {

// 取 url 到 path。max_bytes 内的小文件（清单/签名）。失败写 error。
bool FetchToFile(const std::string& url, const std::string& path,
                 const std::string& ca_file, std::string* error);

}  // namespace ota

#endif  // OTA_VERIFY_FETCH_H_
