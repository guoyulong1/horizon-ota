// 用 fsd::SecureDownloader 逐组件下载到暂存区。
#ifndef OTA_AGENT_DOWNLOAD_H_
#define OTA_AGENT_DOWNLOAD_H_

#include "manifest.h"

#include <string>

namespace ota {

// 下载 manifest 的全部组件到 staging_dir；verbose 时打印进度。
bool DownloadComponents(const Manifest& manifest, const std::string& base_url,
                        const std::string& staging_dir,
                        const std::string& ca_file, bool verbose,
                        std::string* error);

}  // namespace ota

#endif  // OTA_VERIFY_DOWNLOAD_H_
