// Copyright 2026 GuoYuLong. All rights reserved.
// Author: GuoYuLong

#include "fsd/http_client.h"

#include <curl/curl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "fsd/text_util.h"
#include "fsd/url.h"

namespace fsd {
namespace {

struct CurlDeleter {
  void operator()(CURL* curl) const noexcept {
    if (curl != nullptr) {
      curl_easy_cleanup(curl);
    }
  }
};

using CurlPtr = std::unique_ptr<CURL, CurlDeleter>;

// libcurl 的 curl_easy_setopt 是变参函数。在 aarch64 上 long 是 64 位、
// int 是 32 位，枚举和未加 L 的整型常量若直接传入会读错参数。下面的包装
// 把整数固定成 long，把大文件偏移固定成 curl_off_t。
template <typename T>
Status SetOption(CURL* curl, CURLoption option, T value) {
  const CURLcode code = curl_easy_setopt(curl, option, value);
  if (code != CURLE_OK) {
    return Status::Failure(ErrorCode::kNetwork,
                           std::string("failed to set a transfer option: ") +
                               curl_easy_strerror(code));
  }
  return Status::Ok();
}

struct TransferContext {
  FILE* file = nullptr;
  std::uint64_t max_bytes = 0;
  std::uint64_t resume_from = 0;
  std::uint64_t total_written = 0;
  std::optional<std::uint64_t> expected_size;
  std::optional<std::uint64_t> content_length;
  long http_status = 0;
  bool saw_content_length = false;
  bool checked_body = false;
  bool size_exceeded = false;
  bool size_mismatch = false;
  bool io_error = false;
  bool range_rejected = false;
  bool header_invalid = false;
  bool progress_drawn = false;
  bool saw_location = false;
  int saved_errno = 0;
  std::string location;
  bool tty = false;
  int last_percent = -1;
  std::string detail;
};

std::string_view TrimHeaderLine(std::string_view line) {
  while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
    line.remove_suffix(1);
  }
  return line;
}

std::optional<long> ParseStatusCode(std::string_view line) {
  line = TrimHeaderLine(line);
  if (!line.starts_with("HTTP/")) {
    return std::nullopt;
  }
  const std::size_t space = line.find(' ');
  if (space == std::string_view::npos || space + 1 >= line.size()) {
    return std::nullopt;
  }
  const std::string_view rest = line.substr(space + 1);
  int code = 0;
  const std::from_chars_result parsed =
      std::from_chars(rest.data(), rest.data() + rest.size(), code);
  if (parsed.ec != std::errc{} || code < 100 || code > 599) {
    return std::nullopt;
  }
  return static_cast<long>(code);
}

void NoteContentLength(TransferContext* ctx, std::string_view value) {
  const std::optional<std::uint64_t> length = ParseUint64(Trim(value));
  if (!length.has_value()) {
    ctx->header_invalid = true;
    ctx->detail = "Content-Length is not an integer";
    return;
  }
  if (ctx->saw_content_length && ctx->content_length != length) {
    ctx->header_invalid = true;
    ctx->detail = "conflicting Content-Length headers";
    return;
  }
  ctx->content_length = *length;
  ctx->saw_content_length = true;
}

bool IsRedirect(long status) {
  return status == 301 || status == 302 || status == 303 || status == 307 ||
         status == 308;
}

std::size_t WriteHeader(char* buffer, std::size_t size, std::size_t count,
                        void* userdata) {
  if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size) {
    return 0;
  }
  const std::size_t bytes = size * count;
  auto* ctx = static_cast<TransferContext*>(userdata);
  const std::string_view raw(buffer, bytes);
  const std::string_view line = TrimHeaderLine(raw);
  if (line.empty()) {
    return bytes;
  }

  if (const std::optional<long> status = ParseStatusCode(line);
      status.has_value()) {
    // 重定向会送来多段响应头，后出现的状态码才是最终响应。
    ctx->http_status = *status;
    ctx->content_length.reset();
    ctx->saw_content_length = false;
    ctx->location.clear();
    ctx->saw_location = false;
    ctx->checked_body = false;
    return bytes;
  }

  const std::size_t colon = line.find(':');
  if (colon == std::string_view::npos) {
    return bytes;
  }
  const std::string_view name = Trim(line.substr(0, colon));
  const std::string_view value = Trim(line.substr(colon + 1));
  if (EqualsIgnoreCase(name, "Content-Length")) {
    NoteContentLength(ctx, value);
    if (ctx->header_invalid) {
      return 0;
    }
  } else if (EqualsIgnoreCase(name, "Location")) {
    const std::string next(value);
    if (ctx->saw_location && ctx->location != next) {
      ctx->header_invalid = true;
      ctx->detail = "conflicting Location headers";
      return 0;
    }
    ctx->location = next;
    ctx->saw_location = true;
  }
  return bytes;
}

bool ExceedsLimit(std::uint64_t already, std::uint64_t incoming,
                  std::uint64_t limit) {
  return incoming > limit || already > limit || incoming > limit - already;
}

std::size_t WriteBody(char* buffer, std::size_t size, std::size_t count,
                      void* userdata) {
  if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size) {
    return 0;
  }
  const std::size_t bytes = size * count;
  auto* ctx = static_cast<TransferContext*>(userdata);
  if (IsRedirect(ctx->http_status)) {
    return bytes;
  }
  if (!ctx->checked_body) {
    ctx->checked_body = true;
    if (ctx->resume_from > 0 && ctx->http_status != 0 &&
        ctx->http_status != 206) {
      ctx->range_rejected = true;
      ctx->detail =
          "resume expected HTTP 206, got " + std::to_string(ctx->http_status);
      return 0;
    }
    if (ctx->http_status != 0 && ctx->http_status != 200 &&
        ctx->http_status != 206) {
      ctx->detail =
          "unexpected HTTP status " + std::to_string(ctx->http_status);
      return 0;
    }
    if (ctx->saw_content_length && ctx->content_length.has_value()) {
      std::uint64_t total = *ctx->content_length;
      if (ctx->resume_from > 0) {
        if (ExceedsLimit(ctx->resume_from, total,
                         std::numeric_limits<std::uint64_t>::max())) {
          ctx->size_exceeded = true;
          return 0;
        }
        total += ctx->resume_from;
      }
      if (total > ctx->max_bytes) {
        ctx->size_exceeded = true;
        ctx->detail = "declared size " + std::to_string(total) +
                      " exceeds the limit " + std::to_string(ctx->max_bytes);
        return 0;
      }
      if (ctx->expected_size.has_value() && total != *ctx->expected_size) {
        ctx->size_mismatch = true;
        ctx->detail = "response size " + std::to_string(total) +
                      " does not match declared size " +
                      std::to_string(*ctx->expected_size);
        return 0;
      }
    }
  }

  if (bytes == 0) {
    return 0;
  }
  if (ExceedsLimit(ctx->total_written, bytes, ctx->max_bytes)) {
    ctx->size_exceeded = true;
    ctx->detail =
        "download exceeds the size limit " + std::to_string(ctx->max_bytes);
    return 0;
  }

  const std::size_t written = std::fwrite(buffer, 1, bytes, ctx->file);
  if (written != bytes) {
    ctx->io_error = true;
    ctx->saved_errno = errno;
    return written;
  }
  ctx->total_written += written;
  return written;
}

int ReportProgress(void* clientp, curl_off_t total, curl_off_t now,
                   curl_off_t /*upload_total*/, curl_off_t /*uploaded*/) {
  auto* ctx = static_cast<TransferContext*>(clientp);
  if (now < 0) {
    return 0;
  }
  if (total <= 0) {
    if (ctx->tty) {
      std::cerr << "\rreceived " << now << " bytes";
      ctx->progress_drawn = true;
    }
    return 0;
  }

  const int percent = static_cast<int>(std::clamp(
      (static_cast<double>(now) * 100.0) / static_cast<double>(total), 0.0,
      100.0));
  if (percent == ctx->last_percent) {
    return 0;
  }
  ctx->last_percent = percent;
  if (ctx->tty) {
    std::cerr << "\rprogress " << percent << "%  " << now << " / " << total
              << " bytes";
    ctx->progress_drawn = true;
  } else if (percent == 100) {
    std::cerr << "progress 100% (" << now << " / " << total << " bytes)\n";
  }
  return 0;
}

Status EnsureCurlReady() {
  // 不在静态析构里调用 curl_global_cleanup：退出阶段的析构顺序不确定，
  // 其他翻译单元仍可能碰 libcurl。进程退出时由系统回收即可。
  static const CURLcode code = curl_global_init(CURL_GLOBAL_DEFAULT);
  if (code != CURLE_OK) {
    return Status::Failure(ErrorCode::kNetwork,
                           std::string("failed to initialize libcurl: ") +
                               curl_easy_strerror(code));
  }
  return Status::Ok();
}

Status SyncFile(FILE* file) {
  if (std::fflush(file) != 0) {
    return Status::Failure(
        ErrorCode::kIo,
        ErrnoMessage("failed to flush the temporary firmware file"));
  }
  const int fd = fileno(file);
  if (fd < 0 || ::fsync(fd) != 0) {
    return Status::Failure(
        ErrorCode::kIo,
        ErrnoMessage("failed to sync the temporary firmware file"));
  }
  return Status::Ok();
}

Status ApplySecurityOptions(CURL* curl, const HttpRequest& request) {
  auto set = [curl](CURLoption option, auto value) -> Status {
    return SetOption(curl, option, value);
  };

  // 协议锁死在 HTTPS。7.85 起使用协议字符串；3rdparty 中的 8.23 走这条路径。
#if CURL_AT_LEAST_VERSION(7, 85, 0)
  if (Status status = set(CURLOPT_PROTOCOLS_STR, "https"); !status.ok()) {
    return status;
  }
  if (Status status = set(CURLOPT_REDIR_PROTOCOLS_STR, "https"); !status.ok()) {
    return status;
  }
#else
  if (Status status =
          set(CURLOPT_PROTOCOLS, static_cast<long>(CURLPROTO_HTTPS));
      !status.ok()) {
    return status;
  }
  if (Status status =
          set(CURLOPT_REDIR_PROTOCOLS, static_cast<long>(CURLPROTO_HTTPS));
      !status.ok()) {
    return status;
  }
#endif

  // 这两项是安全下载的底线，故意不提供关闭开关。
  if (Status status = set(CURLOPT_SSL_VERIFYPEER, 1L); !status.ok()) {
    return status;
  }
  if (Status status = set(CURLOPT_SSL_VERIFYHOST, 2L); !status.ok()) {
    return status;
  }
  if (Status status = set(CURLOPT_PROXY_SSL_VERIFYPEER, 1L); !status.ok()) {
    return status;
  }
  if (Status status = set(CURLOPT_PROXY_SSL_VERIFYHOST, 2L); !status.ok()) {
    return status;
  }
  // 只设定下限，仍允许 TLS 1.3。
  if (Status status =
          set(CURLOPT_SSLVERSION, static_cast<long>(CURL_SSLVERSION_TLSv1_2));
      !status.ok()) {
    return status;
  }
  // 静态 curl 没编 nghttp2 时，强制 HTTP/2 会被拒绝。有 HTTP/2 才协商，
  // 否则固定 HTTP/1.1，HTTPS 仍然可用。
  const curl_version_info_data* version = curl_version_info(CURLVERSION_NOW);
  const bool has_http2 =
      version != nullptr && (version->features & CURL_VERSION_HTTP2) != 0;
  const long http_version = has_http2
                                ? static_cast<long>(CURL_HTTP_VERSION_2TLS)
                                : static_cast<long>(CURL_HTTP_VERSION_1_1);
  if (Status status = set(CURLOPT_HTTP_VERSION, http_version); !status.ok()) {
    return status;
  }
  // 摘要覆盖的是原始包字节，禁止 libcurl 透明解压。
  if (Status status = set(CURLOPT_HTTP_CONTENT_DECODING, 0L); !status.ok()) {
    return status;
  }
  if (Status status = set(CURLOPT_UNRESTRICTED_AUTH, 0L); !status.ok()) {
    return status;
  }
  if (!request.ca_file.empty()) {
    if (Status status = set(CURLOPT_CAINFO, request.ca_file.c_str());
        !status.ok()) {
      return status;
    }
  }
  if (!request.pinned_public_key.empty()) {
    if (Status status =
            set(CURLOPT_PINNEDPUBLICKEY, request.pinned_public_key.c_str());
        !status.ok()) {
      return status;
    }
  }
  return Status::Ok();
}

Status MapCurlFailure(CURLcode code, const TransferContext& ctx,
                      const char* errbuf, long response_code) {
  if (ctx.size_exceeded) {
    return Status::Failure(ErrorCode::kSizeLimit,
                           ctx.detail.empty()
                               ? "firmware package exceeds the allowed size"
                               : ctx.detail);
  }
  if (ctx.size_mismatch) {
    return Status::Failure(ErrorCode::kInvalidArgument, ctx.detail);
  }
  if (ctx.range_rejected) {
    return Status::Failure(
        ErrorCode::kRangeNotSupported,
        ctx.detail.empty() ? "server does not support resume" : ctx.detail);
  }
  if (ctx.io_error) {
    std::string message = "failed to write the temporary firmware file";
    if (ctx.saved_errno != 0) {
      const std::error_code error(ctx.saved_errno, std::generic_category());
      message.append(": ");
      message.append(error.message());
    }
    return Status::Failure(ErrorCode::kIo, message);
  }
  if (ctx.header_invalid) {
    return Status::Failure(ErrorCode::kHttp, ctx.detail.empty()
                                                 ? "invalid response header"
                                                 : ctx.detail);
  }

  std::string detail = curl_easy_strerror(code);
  if (errbuf != nullptr && errbuf[0] != '\0') {
    detail = errbuf;
  }
  if (response_code > 0) {
    detail = "HTTP " + std::to_string(response_code) + "，" + detail;
  }

  switch (code) {
    case CURLE_SSL_CONNECT_ERROR:
    case CURLE_SSL_CERTPROBLEM:
    case CURLE_SSL_CIPHER:
    case CURLE_PEER_FAILED_VERIFICATION:  // 含 CURLE_SSL_CACERT 这一别名
    case CURLE_SSL_CACERT_BADFILE:
    case CURLE_SSL_PINNEDPUBKEYNOTMATCH:
    case CURLE_SSL_INVALIDCERTSTATUS:
      return Status::Failure(ErrorCode::kTls, detail);
    case CURLE_HTTP_RETURNED_ERROR:
      return Status::Failure(ErrorCode::kHttp, detail);
    case CURLE_TOO_MANY_REDIRECTS:
    case CURLE_UNSUPPORTED_PROTOCOL:
      return Status::Failure(ErrorCode::kProtocol, detail);
    case CURLE_FILESIZE_EXCEEDED:
      return Status::Failure(ErrorCode::kSizeLimit, detail);
    case CURLE_RANGE_ERROR:
      return Status::Failure(ErrorCode::kRangeNotSupported, detail);
    case CURLE_URL_MALFORMAT:
      return Status::Failure(ErrorCode::kInvalidArgument, detail);
    case CURLE_OPERATION_TIMEDOUT:
    case CURLE_COULDNT_RESOLVE_HOST:
    case CURLE_COULDNT_CONNECT:
    case CURLE_RECV_ERROR:
    case CURLE_SEND_ERROR:
    case CURLE_GOT_NOTHING:
    case CURLE_PARTIAL_FILE:
      return Status::Failure(ErrorCode::kNetwork, detail);
    default:
      return Status::Failure(ErrorCode::kNetwork, detail);
  }
}

bool FitsCurlOffset(std::uint64_t value) {
  return value <=
         static_cast<std::uint64_t>(std::numeric_limits<curl_off_t>::max());
}

void ResetHop(TransferContext* context) {
  context->http_status = 0;
  context->content_length.reset();
  context->saw_content_length = false;
  context->checked_body = false;
  context->location.clear();
  context->saw_location = false;
  context->header_invalid = false;
  context->detail.clear();
}

Status BytesOnDisk(FILE* file, std::uint64_t* bytes) {
  const int fd = fileno(file);
  if (fd < 0) {
    return Status::Failure(ErrorCode::kIo,
                           "cannot determine the temporary file size");
  }
  struct stat state {};
  if (::fstat(fd, &state) != 0 || state.st_size < 0) {
    return Status::Failure(
        ErrorCode::kIo,
        ErrnoMessage("cannot determine the temporary file size"));
  }
  *bytes = static_cast<std::uint64_t>(state.st_size);
  return Status::Ok();
}

}  // namespace

Status HttpClient::Download(const HttpRequest& request,
                            HttpResult* result) const {
  if (result == nullptr) {
    return Status::Failure(ErrorCode::kInvalidArgument,
                           "download result argument is null");
  }
  if (request.destination_file == nullptr) {
    return Status::Failure(ErrorCode::kInvalidArgument,
                           "temporary file is not open");
  }
  if (Status ready = EnsureCurlReady(); !ready.ok()) {
    return ready;
  }
  if (!FitsCurlOffset(request.resume_from) ||
      !FitsCurlOffset(request.max_bytes)) {
    return Status::Failure(
        ErrorCode::kInvalidArgument,
        "offset or size limit exceeds the range libcurl can represent");
  }

  CurlPtr curl(curl_easy_init());
  if (!curl) {
    return Status::Failure(ErrorCode::kNetwork,
                           "failed to create a libcurl handle");
  }

  TransferContext context;
  context.file = request.destination_file;
  context.max_bytes = request.max_bytes;
  context.resume_from = request.resume_from;
  context.total_written = request.resume_from;
  context.expected_size = request.expected_size;
  context.tty = ::isatty(fileno(stderr)) == 1;

  char errbuf[CURL_ERROR_SIZE] = {};
  auto set = [&curl](CURLoption option, auto value) -> Status {
    return SetOption(curl.get(), option, value);
  };

  if (Status status = ApplySecurityOptions(curl.get(), request); !status.ok()) {
    return status;
  }
  if (Status status = set(CURLOPT_FOLLOWLOCATION, 0L); !status.ok()) {
    return status;
  }
  if (Status status = set(CURLOPT_FAILONERROR, 1L); !status.ok()) {
    return status;
  }
  if (Status status = set(CURLOPT_NOSIGNAL, 1L); !status.ok()) {
    return status;
  }
  if (Status status = set(CURLOPT_TCP_KEEPALIVE, 1L); !status.ok()) {
    return status;
  }
  if (Status status = set(CURLOPT_LOW_SPEED_LIMIT, 1024L); !status.ok()) {
    return status;
  }
  if (Status status = set(CURLOPT_LOW_SPEED_TIME, 30L); !status.ok()) {
    return status;
  }
  if (Status status = set(CURLOPT_USERAGENT, request.user_agent.c_str());
      !status.ok()) {
    return status;
  }
  if (Status status = set(CURLOPT_WRITEFUNCTION, &WriteBody); !status.ok()) {
    return status;
  }
  if (Status status = set(CURLOPT_WRITEDATA, &context); !status.ok()) {
    return status;
  }
  if (Status status = set(CURLOPT_HEADERFUNCTION, &WriteHeader); !status.ok()) {
    return status;
  }
  if (Status status = set(CURLOPT_HEADERDATA, &context); !status.ok()) {
    return status;
  }
  if (Status status = set(CURLOPT_XFERINFOFUNCTION, &ReportProgress);
      !status.ok()) {
    return status;
  }
  if (Status status = set(CURLOPT_XFERINFODATA, &context); !status.ok()) {
    return status;
  }
  if (Status status = set(CURLOPT_NOPROGRESS, 0L); !status.ok()) {
    return status;
  }
  if (Status status = set(CURLOPT_ERRORBUFFER, errbuf); !status.ok()) {
    return status;
  }
  if (Status status = set(CURLOPT_MAXFILESIZE_LARGE,
                          static_cast<curl_off_t>(request.max_bytes));
      !status.ok()) {
    return status;
  }
  if (request.resume_from > 0) {
    if (Status status = set(CURLOPT_RESUME_FROM_LARGE,
                            static_cast<curl_off_t>(request.resume_from));
        !status.ok()) {
      return status;
    }
  }

  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(request.transfer_timeout_seconds);
  std::string current = request.url;
  std::string final_url;
  for (long hop = 0; hop <= request.max_redirects; ++hop) {
    if (Status status = ValidateUrl(current); !status.ok()) {
      return status;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      return Status::Failure(ErrorCode::kNetwork, "transfer timed out");
    }
    const auto remaining_count =
        std::chrono::duration_cast<std::chrono::seconds>(deadline - now)
            .count();
    long remaining =
        remaining_count > 0 ? static_cast<long>(remaining_count) : 1;
    if (remaining > request.transfer_timeout_seconds) {
      remaining = request.transfer_timeout_seconds;
    }
    const long connect = std::min(request.connect_timeout_seconds, remaining);
    ResetHop(&context);
    errbuf[0] = '\0';
    if (Status status = set(CURLOPT_URL, current.c_str()); !status.ok()) {
      return status;
    }
    if (Status status = set(CURLOPT_CONNECTTIMEOUT, connect); !status.ok()) {
      return status;
    }
    if (Status status = set(CURLOPT_TIMEOUT, remaining); !status.ok()) {
      return status;
    }

    const CURLcode perform = curl_easy_perform(curl.get());
    if (context.progress_drawn && context.tty) {
      std::cerr << '\n';
      context.progress_drawn = false;
    }
    long response_code = 0;
    curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &response_code);
    if (context.http_status == 0) {
      context.http_status = response_code;
    }
    if (perform != CURLE_OK || context.size_exceeded || context.size_mismatch ||
        context.range_rejected || context.io_error || context.header_invalid) {
      const CURLcode reported =
          perform == CURLE_OK ? CURLE_WRITE_ERROR : perform;
      return MapCurlFailure(reported, context, errbuf, response_code);
    }
    if (IsRedirect(context.http_status)) {
      if (hop == request.max_redirects) {
        return Status::Failure(ErrorCode::kProtocol,
                               "redirect count exceeds the limit");
      }
      if (!context.saw_location) {
        return Status::Failure(ErrorCode::kProtocol,
                               "redirect is missing a Location header");
      }
      std::string next;
      if (Status status = ResolveRedirect(current, context.location, &next);
          !status.ok()) {
        return status;
      }
      if (Status status = ValidateUrl(next); !status.ok()) {
        return status;
      }
      current = std::move(next);
      continue;
    }
    if (context.http_status != 200 && context.http_status != 206) {
      return Status::Failure(ErrorCode::kHttp,
                             "HTTP status " +
                                 std::to_string(context.http_status) +
                                 " is not acceptable");
    }
    final_url = current;
    break;
  }
  if (final_url.empty()) {
    return Status::Failure(ErrorCode::kProtocol,
                           "redirect count exceeds the limit");
  }
  if (Status status = ValidateUrl(final_url); !status.ok()) {
    return status;
  }
  if (Status status = SyncFile(request.destination_file); !status.ok()) {
    return status;
  }
  std::uint64_t bytes = 0;
  if (Status status = BytesOnDisk(request.destination_file, &bytes);
      !status.ok()) {
    return status;
  }
  result->http_status = context.http_status;
  result->bytes_on_disk = bytes;
  result->effective_url = final_url;
  return Status::Ok();
}

}  // namespace fsd
