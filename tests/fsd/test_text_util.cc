// Copyright 2026 GuoYuLong. All rights reserved.
// Author: GuoYuLong
//
// text_util.h：清单与命令行共用的文本工具。只接受完整十进制数字，
// 拒绝前导加号和尾部垃圾。

#include "fsd/text_util.h"

#include <cstdint>
#include <limits>
#include <optional>
#include <string>

#include <gtest/gtest.h>

namespace fsd {
namespace {

TEST(Trim, RemovesSurroundingWhitespace) {
  EXPECT_EQ(Trim("  hello world  "), "hello world");
  EXPECT_EQ(Trim("\t\r\nkey=value\n"), "key=value");
  EXPECT_EQ(Trim("no-padding"), "no-padding");
}

TEST(Trim, KeepsInteriorWhitespace) {
  EXPECT_EQ(Trim("a b"), "a b");
}

TEST(Trim, HandlesEmptyAndBlankInput) {
  EXPECT_EQ(Trim(""), "");
  EXPECT_EQ(Trim("   "), "");
  EXPECT_EQ(Trim("\t\n\r\v\f"), "");
}

TEST(EqualsIgnoreCase, MatchesIgnoringCase) {
  EXPECT_TRUE(EqualsIgnoreCase("Content-Length", "content-length"));
  EXPECT_TRUE(EqualsIgnoreCase("HTTPS", "https"));
  EXPECT_TRUE(EqualsIgnoreCase("", ""));
  EXPECT_FALSE(EqualsIgnoreCase("https", "http"));
  EXPECT_FALSE(EqualsIgnoreCase("abc", "abcd"));
  EXPECT_FALSE(EqualsIgnoreCase("", "a"));
}

TEST(EqualsIgnoreCase, ComparesNonLettersExactly) {
  EXPECT_TRUE(EqualsIgnoreCase("a-b1", "A-B1"));
  EXPECT_FALSE(EqualsIgnoreCase("a_b", "A-B"));
}

TEST(StartsWithIgnoreCase, MatchesPrefixIgnoringCase) {
  EXPECT_TRUE(StartsWithIgnoreCase("HTTPS://example.com", "https://"));
  EXPECT_TRUE(StartsWithIgnoreCase("https://", "HTTPS://"));
  EXPECT_TRUE(
      StartsWithIgnoreCase("https://example.com", "https://example.com"));
  EXPECT_TRUE(StartsWithIgnoreCase("anything", ""));
}

TEST(StartsWithIgnoreCase, RejectsLongerPrefix) {
  EXPECT_FALSE(StartsWithIgnoreCase("http://", "https://"));
  EXPECT_FALSE(StartsWithIgnoreCase("https", "https://"));
  EXPECT_FALSE(StartsWithIgnoreCase("", "a"));
}

TEST(ParseUint64, AcceptsPlainDecimalDigits) {
  EXPECT_EQ(ParseUint64("0"), std::optional<std::uint64_t>(0));
  EXPECT_EQ(ParseUint64("42"), std::optional<std::uint64_t>(42));
  // 前导零按十进制收，不做八进制歧义。
  EXPECT_EQ(ParseUint64("007"), std::optional<std::uint64_t>(7));
  EXPECT_EQ(ParseUint64("18446744073709551615"),
            std::optional<std::uint64_t>(
                std::numeric_limits<std::uint64_t>::max()));
}

TEST(ParseUint64, RejectsSignsEmptyAndGarbage) {
  EXPECT_EQ(ParseUint64(""), std::nullopt);
  EXPECT_EQ(ParseUint64("+1"), std::nullopt);
  EXPECT_EQ(ParseUint64("-1"), std::nullopt);
  EXPECT_EQ(ParseUint64("12x"), std::nullopt);
  EXPECT_EQ(ParseUint64("x12"), std::nullopt);
  EXPECT_EQ(ParseUint64(" 12"), std::nullopt);
  EXPECT_EQ(ParseUint64("12 "), std::nullopt);
}

TEST(ParseUint64, RejectsOverflow) {
  EXPECT_EQ(ParseUint64("18446744073709551616"), std::nullopt);
  EXPECT_EQ(ParseUint64("99999999999999999999999"), std::nullopt);
}

TEST(ParseLong, AcceptsNegativeValuesAndRangeEdge) {
  EXPECT_EQ(ParseLong("0"), std::optional<long>(0));
  EXPECT_EQ(ParseLong("-5"), std::optional<long>(-5));
  EXPECT_EQ(ParseLong("300"), std::optional<long>(300));
  if constexpr (std::numeric_limits<long>::digits == 63) {
    EXPECT_EQ(ParseLong("-9223372036854775808"),
              std::optional<long>(std::numeric_limits<long>::min()));
  }
}

TEST(ParseLong, RejectsPlusSignEmptyOverflowAndGarbage) {
  EXPECT_EQ(ParseLong(""), std::nullopt);
  EXPECT_EQ(ParseLong("+5"), std::nullopt);
  EXPECT_EQ(ParseLong("3sec"), std::nullopt);
  EXPECT_EQ(ParseLong("--3"), std::nullopt);
  if constexpr (std::numeric_limits<long>::digits == 63) {
    EXPECT_EQ(ParseLong("9223372036854775808"), std::nullopt);
  }
}

}  // namespace
}  // namespace fsd
