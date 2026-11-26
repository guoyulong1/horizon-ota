// Copyright 2026 GuoYuLong. All rights reserved.
// Author: GuoYuLong

#include "fsd/url.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>

#include "fsd/text_util.h"

namespace fsd {
namespace {

constexpr std::string_view kHttpsScheme = "https://";
constexpr std::size_t kMaxUrlLength = 2048;
// RFC 4291 §2.2 带 IPv4 尾的最长文本是 45 个字符：
// xxxx:xxxx:xxxx:xxxx:xxxx:xxxx:255.255.255.255
constexpr std::size_t kMaxIpv6TextLength = 45;

bool IsHexDigit(char ch) {
  return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
         (ch >= 'A' && ch <= 'F');
}

bool IsIpv4(std::string_view host) {
  int octets = 0;
  while (!host.empty()) {
    const std::size_t dot = host.find('.');
    const bool last = dot == std::string_view::npos;
    const std::string_view piece = last ? host : host.substr(0, dot);
    if (piece.empty() || piece.size() > 3 ||
        (piece.size() > 1 && piece.front() == '0')) {
      return false;
    }
    int value = 0;
    for (const char ch : piece) {
      if (ch < '0' || ch > '9') {
        return false;
      }
      value = value * 10 + (ch - '0');
    }
    if (value > 255) {
      return false;
    }
    ++octets;
    if (last) {
      break;
    }
    host.remove_prefix(dot + 1);
    if (host.empty()) {
      return false;
    }
  }
  return octets == 4;
}

// 解析 :: 的一侧。IPv4 尾只允许出现在整个地址的末尾。
bool ParseIpv6Side(std::string_view side, bool allow_ipv4, int* groups) {
  *groups = 0;
  if (side.empty()) {
    return true;
  }
  while (!side.empty()) {
    const std::size_t colon = side.find(':');
    const bool last = colon == std::string_view::npos;
    const std::string_view piece = last ? side : side.substr(0, colon);
    if (piece.empty()) {
      return false;
    }
    if (piece.find('.') != std::string_view::npos) {
      if (!allow_ipv4 || !last || !IsIpv4(piece)) {
        return false;
      }
      *groups += 2;
    } else {
      if (piece.size() > 4) {
        return false;
      }
      for (const char ch : piece) {
        if (!IsHexDigit(ch)) {
          return false;
        }
      }
      *groups += 1;
    }
    if (last) {
      break;
    }
    side.remove_prefix(colon + 1);
    if (side.empty()) {
      return false;
    }
  }
  return true;
}

bool IsIpv6(std::string_view host) {
  if (host.empty() || host.size() > kMaxIpv6TextLength) {
    return false;
  }
  const std::size_t mark = host.find("::");
  const bool compressed = mark != std::string_view::npos;
  if (compressed && host.find("::", mark + 2) != std::string_view::npos) {
    return false;
  }
  int left = 0;
  int right = 0;
  if (!compressed) {
    return ParseIpv6Side(host, true, &left) && left == 8;
  }
  // IPv4 尾只能在右侧，左侧出现点分地址不是合法写法。
  if (!ParseIpv6Side(host.substr(0, mark), false, &left) ||
      !ParseIpv6Side(host.substr(mark + 2), true, &right)) {
    return false;
  }
  // :: 至少要顶替一组零，所以显式组数必须小于 8。
  return left + right < 8;
}

bool IsDnsName(std::string_view host) {
  if (host.empty() || host.size() > 253) {
    return false;
  }
  if (host.back() == '.') {
    host.remove_suffix(1);
    if (host.empty() || host.size() > 253) {
      return false;
    }
  }
  int label_length = 0;
  char previous = '.';
  for (const char ch : host) {
    if (ch == '.') {
      if (label_length == 0 || previous == '-') {
        return false;
      }
      label_length = 0;
      previous = ch;
      continue;
    }
    const bool alnum = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                       (ch >= '0' && ch <= '9');
    if (!alnum && ch != '-') {
      return false;
    }
    if (label_length == 0 && ch == '-') {
      return false;
    }
    if (++label_length > 63) {
      return false;
    }
    previous = ch;
  }
  return label_length > 0 && previous != '-';
}

// 空串被 ParseUint64 拒绝。"080" 按十进制收成 80，符合 RFC 3986 的 *DIGIT。
// 端口没有 IPv4 那种八进制歧义，所以保留前导零。
bool IsPort(std::string_view port) {
  const std::optional<std::uint64_t> value = ParseUint64(port);
  return value.has_value() && *value >= 1 && *value <= 65535;
}

Status InvalidHost() {
  return Status::Failure(ErrorCode::kInvalidArgument, "URL host is invalid");
}

Status ValidateAuthority(std::string_view authority) {
  if (authority.empty()) {
    return InvalidHost();
  }
  if (authority.find('@') != std::string_view::npos) {
    return Status::Failure(ErrorCode::kInvalidArgument,
                           "URL contains user info that does not belong in a "
                           "firmware URL");
  }

  std::string_view host;
  std::optional<std::string_view> port;
  if (authority.front() == '[') {
    const std::size_t close = authority.find(']');
    if (close == std::string_view::npos) {
      return InvalidHost();
    }
    host = authority.substr(1, close - 1);
    const std::string_view tail = authority.substr(close + 1);
    if (!tail.empty()) {
      if (tail.front() != ':') {
        return InvalidHost();
      }
      port = tail.substr(1);
    }
    if (!IsIpv6(host)) {
      return InvalidHost();
    }
  } else {
    const std::size_t colon = authority.find(':');
    if (colon == std::string_view::npos) {
      host = authority;
    } else if (authority.find(':', colon + 1) != std::string_view::npos) {
      return InvalidHost();
    } else {
      host = authority.substr(0, colon);
      port = authority.substr(colon + 1);
    }
    // 只含数字和点就按 IPv4 检查。999.1.1.1 这类看起来像地址但不合法的写法
    // 直接拒绝，不退回 DNS 名称。
    const bool dotted_decimal =
        !host.empty() &&
        host.find_first_not_of("0123456789.") == std::string_view::npos;
    if (host.empty() || (dotted_decimal ? !IsIpv4(host) : !IsDnsName(host))) {
      return InvalidHost();
    }
  }
  if (port.has_value() && !IsPort(*port)) {
    return Status::Failure(ErrorCode::kInvalidArgument, "URL port is invalid");
  }
  return Status::Ok();
}

// curl 会把非法的 % 序列原样发出，所以 path、query、fragment 在这里拦截。
Status ValidatePercentEncoding(std::string_view text) {
  for (std::size_t index = 0; index < text.size(); ++index) {
    if (text[index] != '%') {
      continue;
    }
    if (index + 2 >= text.size() || !IsHexDigit(text[index + 1]) ||
        !IsHexDigit(text[index + 2])) {
      return Status::Failure(ErrorCode::kInvalidArgument,
                             "URL contains an invalid percent-encoding");
    }
    index += 2;
  }
  return Status::Ok();
}

std::size_t AuthorityEnd(std::string_view rest) {
  std::size_t authority_end = rest.size();
  for (const char boundary : {'/', '?', '#'}) {
    const std::size_t pos = rest.find(boundary);
    if (pos != std::string_view::npos) {
      authority_end = std::min(authority_end, pos);
    }
  }
  return authority_end;
}

std::string RemoveDotSegments(std::string_view input) {
  std::string output;
  while (!input.empty()) {
    if (input.starts_with("../")) {
      // RFC 3986 §5.2.4：把前缀 "../" 替换成 "/"，而不是整个删掉。
      // 少掉的这个斜杠会让 output 的末段与后一段直接粘连
      // （例如 "/a/../../b" 会算出 "ab"）。
      input.remove_prefix(2);
    } else if (input.starts_with("./")) {
      input.remove_prefix(2);
    } else if (input.starts_with("/./")) {
      input.remove_prefix(2);
    } else if (input == "/.") {
      input = "/";
    } else if (input.starts_with("/../")) {
      input.remove_prefix(3);
      const auto slash = output.rfind('/');
      if (slash == std::string::npos) {
        output.clear();
      } else {
        output.resize(slash);
      }
    } else if (input == "/..") {
      input = "/";
      const auto slash = output.rfind('/');
      if (slash == std::string::npos) {
        output.clear();
      } else {
        output.resize(slash);
      }
    } else if (input == "." || input == "..") {
      input = {};
    } else {
      const std::size_t start = input.front() == '/' ? 1 : 0;
      const auto next = input.find('/', start);
      const std::size_t count =
          next == std::string_view::npos ? input.size() : next;
      output.append(input.substr(0, count));
      input.remove_prefix(count);
    }
  }
  if (output.empty()) {
    output = "/";
  }
  return output;
}

void SplitPathAndQuery(std::string_view after, std::string_view* path,
                       std::string_view* query) {
  const auto hash = after.find('#');
  if (hash != std::string_view::npos) {
    after = after.substr(0, hash);
  }
  const auto question = after.find('?');
  if (question == std::string_view::npos) {
    *path = after;
    *query = {};
    return;
  }
  *path = after.substr(0, question);
  *query = after.substr(question);
}

}  // namespace

Status ValidateUrl(std::string_view url) {
  if (url.size() < kHttpsScheme.size() || url.size() > kMaxUrlLength) {
    return Status::Failure(ErrorCode::kInvalidArgument,
                           "URL length is invalid");
  }
  for (const char ch : url) {
    const auto byte = static_cast<unsigned char>(ch);
    if (byte <= 0x20 || byte >= 0x7F) {
      return Status::Failure(ErrorCode::kInvalidArgument,
                             "URL contains whitespace, a control character, or "
                             "a non-ASCII byte");
    }
  }
  if (!StartsWithIgnoreCase(url, kHttpsScheme)) {
    return Status::Failure(
        ErrorCode::kProtocol,
        "firmware packages can be downloaded only over HTTPS");
  }

  const std::string_view rest = url.substr(kHttpsScheme.size());
  const std::size_t authority_end = AuthorityEnd(rest);
  if (Status status = ValidateAuthority(rest.substr(0, authority_end));
      !status.ok()) {
    return status;
  }
  return ValidatePercentEncoding(rest.substr(authority_end));
}

Status ResolveRedirect(std::string_view base, std::string_view location,
                       std::string* resolved) {
  if (resolved == nullptr || location.empty()) {
    return Status::Failure(ErrorCode::kInvalidArgument,
                           "redirect target is missing");
  }
  const auto hash = location.find('#');
  if (hash != std::string_view::npos) {
    location = location.substr(0, hash);
  }
  if (location.empty()) {
    return Status::Failure(ErrorCode::kInvalidArgument,
                           "redirect target is only a fragment");
  }

  const auto scheme_colon = location.find(':');
  const auto first_slash = location.find('/');
  const bool absolute =
      scheme_colon != std::string_view::npos &&
      (first_slash == std::string_view::npos || scheme_colon < first_slash);
  if (absolute) {
    *resolved = std::string(location);
    return Status::Ok();
  }
  if (location.starts_with("//")) {
    *resolved = "https:" + std::string(location);
    return Status::Ok();
  }
  if (!StartsWithIgnoreCase(base, kHttpsScheme)) {
    return Status::Failure(ErrorCode::kInvalidArgument,
                           "redirect base URL is not HTTPS");
  }

  const std::string_view rest = base.substr(kHttpsScheme.size());
  const std::size_t authority_end = AuthorityEnd(rest);
  const std::string authority(rest.substr(0, authority_end));
  std::string_view base_path;
  std::string_view base_query;
  SplitPathAndQuery(rest.substr(authority_end), &base_path, &base_query);

  std::string path;
  std::string_view query;
  if (location.front() == '?') {
    path = base_path.empty() ? std::string("/") : std::string(base_path);
    query = location;
  } else if (location.front() == '/') {
    std::string_view loc_path;
    SplitPathAndQuery(location, &loc_path, &query);
    path = RemoveDotSegments(loc_path);
  } else {
    std::string_view relative;
    SplitPathAndQuery(location, &relative, &query);
    std::string prefix = "/";
    if (!base_path.empty()) {
      const auto slash = base_path.rfind('/');
      prefix = slash == std::string_view::npos
                   ? std::string("/")
                   : std::string(base_path.substr(0, slash + 1));
    }
    path = RemoveDotSegments(prefix + std::string(relative));
  }
  *resolved = std::string(kHttpsScheme) + authority + path + std::string(query);
  return Status::Ok();
}

}  // namespace fsd
