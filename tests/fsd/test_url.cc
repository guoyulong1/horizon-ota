// Copyright 2026 GuoYuLong. All rights reserved.
// Author: GuoYuLong
//
// url.cc：固件 URL 只接受绝对 HTTPS，初始地址和重定向共用同一套检查。
// 重定向解析按 RFC 3986 合并相对引用。

#include "fsd/url.h"

#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "fsd/status.h"

namespace fsd {
namespace {

bool IsInvalidArgument(const Status& status) {
  return !status.ok() && status.code() == ErrorCode::kInvalidArgument;
}

void ExpectRejected(std::string_view url, ErrorCode code) {
  const Status status = ValidateUrl(url);
  EXPECT_FALSE(status.ok()) << "unexpectedly accepted: " << url;
  EXPECT_EQ(status.code(), code)
      << url << " -> " << status.message();
}

TEST(ValidateUrl, AcceptsHttpsDnsHost) {
  EXPECT_TRUE(ValidateUrl("https://example.com/firmware.bin").ok());
  EXPECT_TRUE(ValidateUrl("https://example.com").ok());
  EXPECT_TRUE(
      ValidateUrl("https://Example.COM:443/a/b.bin?token=x#frag").ok());
  // scheme 大小写不敏感。
  EXPECT_TRUE(ValidateUrl("HTTPS://EXAMPLE.COM/PATH").ok());
}

TEST(ValidateUrl, AcceptsIpv4AndIpv6Hosts) {
  EXPECT_TRUE(ValidateUrl("https://192.168.31.35/fw.bin").ok());
  EXPECT_TRUE(ValidateUrl("https://[::1]/fw.bin").ok());
  EXPECT_TRUE(ValidateUrl("https://[fd00::1]:8443/fw.bin").ok());
  EXPECT_TRUE(ValidateUrl(
                  "https://[1111:2222:3333:4444:5555:6666:7777:8888]/")
                  .ok());
  EXPECT_TRUE(ValidateUrl("https://[::ffff:192.168.31.35]/fw.bin").ok());
}

TEST(ValidateUrl, AcceptsLeadingZeroPortAsDecimal) {
  EXPECT_TRUE(ValidateUrl("https://example.com:8443/fw.bin").ok());
  EXPECT_TRUE(ValidateUrl("https://example.com:080/fw.bin").ok());
  EXPECT_TRUE(ValidateUrl("https://example.com:65535/").ok());
}

TEST(ValidateUrl, RejectsNonHttpsSchemes) {
  ExpectRejected("", ErrorCode::kInvalidArgument);  // 长度不足
  ExpectRejected("http://example.com/fw.bin", ErrorCode::kProtocol);
  ExpectRejected("ftp://example.com/fw.bin", ErrorCode::kProtocol);
  ExpectRejected("file:///etc/firmware.bin", ErrorCode::kProtocol);
  ExpectRejected("example.com/fw.bin", ErrorCode::kProtocol);
}

TEST(ValidateUrl, RejectsWhitespaceControlAndNonAsciiBytes) {
  ExpectRejected("https://exa mple.com/fw.bin", ErrorCode::kInvalidArgument);
  ExpectRejected(std::string("https://example.com/fw\t.bin"),
                 ErrorCode::kInvalidArgument);
  ExpectRejected(std::string("https://example.com/fw\r.bin"),
                 ErrorCode::kInvalidArgument);
  ExpectRejected(std::string("https://example.com/\x7F"),
                 ErrorCode::kInvalidArgument);
  ExpectRejected(std::string("https://example.com/\xEF\xBF\xBD"),
                 ErrorCode::kInvalidArgument);
}

TEST(ValidateUrl, RejectsUserInfo) {
  ExpectRejected("https://user:pass@example.com/fw.bin",
                 ErrorCode::kInvalidArgument);
  ExpectRejected("https://user@example.com/", ErrorCode::kInvalidArgument);
}

TEST(ValidateUrl, RejectsMissingOrMalformedHost) {
  ExpectRejected("https://", ErrorCode::kInvalidArgument);
  ExpectRejected("https:///path", ErrorCode::kInvalidArgument);
  ExpectRejected("https://.example.com/", ErrorCode::kInvalidArgument);
  ExpectRejected("https://a..b/", ErrorCode::kInvalidArgument);
  ExpectRejected("https://exa_mple.com/", ErrorCode::kInvalidArgument);
  ExpectRejected("https://-example.com/", ErrorCode::kInvalidArgument);
  ExpectRejected("https://example-.com/", ErrorCode::kInvalidArgument);
}

TEST(ValidateUrl, RejectsMalformedIpv4LookalikeHost) {
  // 只含数字和点的主机按 IPv4 严格检查，不退回 DNS 名称。
  ExpectRejected("https://999.1.1.1/fw.bin", ErrorCode::kInvalidArgument);
  ExpectRejected("https://1.2.3/fw.bin", ErrorCode::kInvalidArgument);
  ExpectRejected("https://01.2.3.4/fw.bin", ErrorCode::kInvalidArgument);
  ExpectRejected("https://1.2.3.4.5/fw.bin", ErrorCode::kInvalidArgument);
  ExpectRejected("https://256.256.256.256/", ErrorCode::kInvalidArgument);
}

TEST(ValidateUrl, RejectsMalformedIpv6Host) {
  ExpectRejected("https://[1::2::3]/", ErrorCode::kInvalidArgument);
  ExpectRejected("https://[gg::1]/", ErrorCode::kInvalidArgument);
  ExpectRejected("https://[fd00::1/fw.bin", ErrorCode::kInvalidArgument);
  ExpectRejected(
      "https://[1111:2222:3333:4444:5555:6666:7777:8888:9999]/",
      ErrorCode::kInvalidArgument);
  ExpectRejected("https://[::ffff:1.2.3]/", ErrorCode::kInvalidArgument);
  ExpectRejected("https://[::1]80/", ErrorCode::kInvalidArgument);
}

TEST(ValidateUrl, RejectsInvalidPort) {
  ExpectRejected("https://example.com:0/", ErrorCode::kInvalidArgument);
  ExpectRejected("https://example.com:65536/", ErrorCode::kInvalidArgument);
  ExpectRejected("https://example.com:abc/", ErrorCode::kInvalidArgument);
  ExpectRejected("https://example.com:/", ErrorCode::kInvalidArgument);
  ExpectRejected("https://example.com:443:8080/",
                 ErrorCode::kInvalidArgument);
  ExpectRejected("https://[::1]:x/", ErrorCode::kInvalidArgument);
}

TEST(ValidateUrl, RejectsInvalidPercentEncoding) {
  EXPECT_TRUE(ValidateUrl("https://example.com/a%20b/fw.bin?q=%2Fx#f").ok());
  ExpectRejected("https://example.com/%zz", ErrorCode::kInvalidArgument);
  ExpectRejected("https://example.com/%1", ErrorCode::kInvalidArgument);
  ExpectRejected("https://example.com/a%2", ErrorCode::kInvalidArgument);
  ExpectRejected("https://example.com/a%", ErrorCode::kInvalidArgument);
  ExpectRejected("https://example.com/100%end", ErrorCode::kInvalidArgument);
}

TEST(ValidateUrl, EnforcesLengthBounds) {
  std::string at_limit = "https://example.com/";
  at_limit.append(2048 - at_limit.size(), 'a');
  ASSERT_EQ(at_limit.size(), 2048U);
  EXPECT_TRUE(ValidateUrl(at_limit).ok());

  at_limit.push_back('a');
  ExpectRejected(at_limit, ErrorCode::kInvalidArgument);
}

TEST(ResolveRedirect, KeepsAbsoluteHttpsLocation) {
  std::string resolved;
  const Status status =
      ResolveRedirect("https://example.com/a",
                      "https://cdn.example.com/fw.bin", &resolved);
  ASSERT_TRUE(status.ok()) << status.message();
  EXPECT_EQ(resolved, "https://cdn.example.com/fw.bin");
}

// 解析器不强制 HTTPS：绝对 Location 原样返回，调用方（http_client）会再对
// 结果跑一次 ValidateUrl，那里负责拒绝明文协议。
TEST(ResolveRedirect, PassesAbsoluteLocationThroughUnchanged) {
  std::string resolved;
  ASSERT_TRUE(ResolveRedirect("https://example.com/a",
                              "http://example.com/fw.bin", &resolved).ok());
  EXPECT_EQ(resolved, "http://example.com/fw.bin");
}

TEST(ResolveRedirect, KeepsSchemeRelativeLocationOnHttps) {
  std::string resolved;
  ASSERT_TRUE(ResolveRedirect("https://example.com/dir/page",
                              "//cdn.example.com/fw.bin", &resolved).ok());
  EXPECT_EQ(resolved, "https://cdn.example.com/fw.bin");
}

TEST(ResolveRedirect, ResolvesRootRelativePath) {
  std::string resolved;
  ASSERT_TRUE(ResolveRedirect("https://example.com/dir/page?q=1", "/fw.bin",
                              &resolved).ok());
  EXPECT_EQ(resolved, "https://example.com/fw.bin");
}

TEST(ResolveRedirect, ResolvesRelativePathAgainstBaseDirectory) {
  std::string resolved;
  ASSERT_TRUE(ResolveRedirect("https://example.com/dir/page", "fw.bin",
                              &resolved).ok());
  EXPECT_EQ(resolved, "https://example.com/dir/fw.bin");
  ASSERT_TRUE(ResolveRedirect("https://example.com/dir/sub/", "fw.bin",
                              &resolved).ok());
  EXPECT_EQ(resolved, "https://example.com/dir/sub/fw.bin");
}

TEST(ResolveRedirect, ResolvesWithoutBasePath) {
  std::string resolved;
  ASSERT_TRUE(ResolveRedirect("https://example.com", "fw.bin", &resolved)
                  .ok());
  EXPECT_EQ(resolved, "https://example.com/fw.bin");
}

TEST(ResolveRedirect, ReplacesQueryWithLocationQuery) {
  std::string resolved;
  ASSERT_TRUE(ResolveRedirect("https://example.com/p?a=1", "?a=2", &resolved)
                  .ok());
  EXPECT_EQ(resolved, "https://example.com/p?a=2");
  // 相对引用不带 query 时，基地址的 query 不保留（RFC 3986）。
  ASSERT_TRUE(ResolveRedirect("https://example.com/p?a=1", "fw.bin",
                              &resolved).ok());
  EXPECT_EQ(resolved, "https://example.com/fw.bin");
}

TEST(ResolveRedirect, DropsFragmentFromLocation) {
  std::string resolved;
  ASSERT_TRUE(ResolveRedirect("https://example.com/dir/page",
                              "fw.bin#section", &resolved).ok());
  EXPECT_EQ(resolved, "https://example.com/dir/fw.bin");
}

TEST(ResolveRedirect, AppliesDotSegmentRemoval) {
  std::string resolved;
  ASSERT_TRUE(ResolveRedirect("https://example.com/a/b/page", "../fw.bin",
                              &resolved).ok());
  EXPECT_EQ(resolved, "https://example.com/a/fw.bin");
  ASSERT_TRUE(ResolveRedirect("https://example.com/a/b/page", "./fw.bin",
                              &resolved).ok());
  EXPECT_EQ(resolved, "https://example.com/a/b/fw.bin");
  ASSERT_TRUE(ResolveRedirect("https://example.com/a/b/page",
                              "/x/./y/../z.bin", &resolved).ok());
  EXPECT_EQ(resolved, "https://example.com/x/z.bin");
}

TEST(ResolveRedirect, KeepsSeparatorAcrossExtraDotDot) {
  // 回归：连续的 ".." 段会把已消费段之前的 "/" 吞掉，结果 host 和路径
  // 直接粘连成 "example.comfw.bin"。
  std::string resolved;
  ASSERT_TRUE(ResolveRedirect("https://example.com/a/b/page",
                              "/x/./../../fw.bin", &resolved).ok());
  EXPECT_EQ(resolved, "https://example.com/fw.bin");
  ASSERT_TRUE(ResolveRedirect("https://example.com/a", "./../fw.bin",
                              &resolved).ok());
  EXPECT_EQ(resolved, "https://example.com/fw.bin");
}

TEST(ResolveRedirect, RejectsMissingTargetAndNonHttpsBase) {
  std::string resolved;
  EXPECT_TRUE(
      IsInvalidArgument(ResolveRedirect("https://example.com/", "", &resolved)));
  EXPECT_TRUE(IsInvalidArgument(
      ResolveRedirect("https://example.com/", "#frag", &resolved)));
  EXPECT_TRUE(IsInvalidArgument(
      ResolveRedirect("https://example.com/", "fw.bin", nullptr)));
  EXPECT_TRUE(IsInvalidArgument(
      ResolveRedirect("http://example.com/", "fw.bin", &resolved)));
}

}  // namespace
}  // namespace fsd
