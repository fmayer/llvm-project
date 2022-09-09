//===- ASanStackFrameLayoutTest.cpp - Tests for ComputeASanStackFrameLayout===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "llvm/Transforms/Utils/ASanStackFrameLayout.h"
#include "llvm/ADT/ArrayRef.h"
#include "gtest/gtest.h"
#include <sstream>

using namespace llvm;

static std::string
ShadowBytesToString(ArrayRef<uint8_t> ShadowBytes) {
  std::ostringstream os;
  for (size_t i = 0, n = ShadowBytes.size(); i < n; i++) {
    switch (ShadowBytes[i]) {
      case kAsanStackFrontRedzoneMagic:    os << "F"; break;
      case kAsanStackBackRedzoneMagic:   os << "B"; break;
      case kAsanStackMidRedzoneMagic:     os << "M"; break;
      case kAsanStackUseAfterScopeMagic:
        os << "S";
        break;
      default:                            os << (unsigned)ShadowBytes[i];
    }
  }
  return os.str();
}

// Use macro to preserve line information in EXPECT_EQ output.
#define TEST_LAYOUT(V, Granularity, MinHeaderSize, ExpectedDescr,              \
                    ExpectedShadow, ExpectedShadowAfterScope)                  \
  {                                                                            \
    SmallVector<ASanStackVariableDescription, 10> Vars = V;                    \
    ASanStackFrameLayout L =                                                   \
        ComputeASanStackFrameLayout(Vars, Granularity, MinHeaderSize);         \
    EXPECT_STREQ(ExpectedDescr,                                                \
                 ComputeASanStackFrameDescription(Vars).c_str());              \
    EXPECT_EQ(ExpectedShadow, ShadowBytesToString(GetShadowBytes(Vars, L)));   \
    EXPECT_EQ(ExpectedShadowAfterScope,                                        \
              ShadowBytesToString(GetShadowBytesAfterScope(Vars, L)));         \
  }

TEST(ASanStackFrameLayout, Test) {
#define VAR(name, size, lifetime, alignment, line)                             \
  ASanStackVariableDescription name##size##_##alignment = {                    \
    #name #size "_" #alignment,                                                \
    size,                                                                      \
    lifetime,                                                                  \
    alignment,                                                                 \
    0,                                                                         \
    0,                                                                         \
    line,                                                                      \
  }

  VAR(a, 1, 0, 1, 0);
  VAR(p, 1, 0, 32, 15);
  VAR(p, 1, 0, 256, 2700);
  VAR(a, 2, 0, 1, 0);
  VAR(a, 3, 0, 1, 0);
  VAR(a, 4, 0, 1, 0);
  VAR(a, 7, 0, 1, 0);
  VAR(a, 8, 8, 1, 0);
  VAR(a, 9, 0, 1, 0);
  VAR(a, 16, 16, 1, 0);
  VAR(a, 41, 9, 1, 7);
  VAR(a, 105, 103, 1, 0);
  VAR(a, 200, 97, 1, 0);

  TEST_LAYOUT({a1_1}, 8, 16, "1 16 1 4 a1_1", "FF1B", "FF1B");
  TEST_LAYOUT({a1_1}, 16, 16, "1 16 1 4 a1_1", "F1B", "F1B");
  TEST_LAYOUT({a1_1}, 32, 32, "1 32 1 4 a1_1", "F1B", "F1B");
  TEST_LAYOUT({a1_1}, 64, 64, "1 64 1 4 a1_1", "F1B", "F1B");
  TEST_LAYOUT({p1_32}, 8, 32, "1 32 1 8 p1_32:15", "FFFF1BBB", "FFFF1BBB");
  TEST_LAYOUT({p1_32}, 8, 64, "1 64 1 8 p1_32:15", "FFFFFFFF1BBBBBBB",
              "FFFFFFFF1BBBBBBB");

  TEST_LAYOUT({a1_1}, 8, 32, "1 32 1 4 a1_1", "FFFF1BBB", "FFFF1BBB");
  TEST_LAYOUT({a2_1}, 8, 32, "1 32 2 4 a2_1", "FFFF2BBB", "FFFF2BBB");
  TEST_LAYOUT({a3_1}, 8, 32, "1 32 3 4 a3_1", "FFFF3BBB", "FFFF3BBB");
  TEST_LAYOUT({a4_1}, 8, 32, "1 32 4 4 a4_1", "FFFF4BBB", "FFFF4BBB");
  TEST_LAYOUT({a7_1}, 8, 32, "1 32 7 4 a7_1", "FFFF7BBB", "FFFF7BBB");
  TEST_LAYOUT({a8_1}, 8, 32, "1 32 8 4 a8_1", "FFFF0BBB", "FFFFSBBB");
  TEST_LAYOUT({a9_1}, 8, 32, "1 32 9 4 a9_1", "FFFF01BB", "FFFF01BB");
  TEST_LAYOUT({a16_1}, 8, 32, "1 32 16 5 a16_1", "FFFF00BB", "FFFFSSBB");
  TEST_LAYOUT({p1_256}, 8, 32, "1 256 1 11 p1_256:2700",
              "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF1BBB",
              "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF1BBB");
  TEST_LAYOUT({a41_1}, 8, 32, "1 32 41 7 a41_1:7", "FFFF000001BBBBBB",
              "FFFFSS0001BBBBBB");
  TEST_LAYOUT({a105_1}, 8, 32, "1 32 105 6 a105_1", "FFFF00000000000001BBBBBB",
              "FFFFSSSSSSSSSSSSS1BBBBBB");

  {
    SmallVector<ASanStackVariableDescription, 10> t = {a1_1, p1_256};
    TEST_LAYOUT(t, 8, 32, "2 256 1 11 p1_256:2700 272 1 4 a1_1",
                "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF1M1B",
                "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF1M1B");
  }

  {
    SmallVector<ASanStackVariableDescription, 10> t = {a1_1, a16_1, a41_1};
    TEST_LAYOUT(t, 8, 32, "3 32 1 4 a1_1 48 16 5 a16_1 80 41 7 a41_1:7",
                "FFFF1M00MM000001BBBB", "FFFF1MSSMMSS0001BBBB");
  }

  TEST_LAYOUT({a2_1}, 32, 32, "1 32 2 4 a2_1", "F2B", "F2B");
  TEST_LAYOUT({a9_1}, 32, 32, "1 32 9 4 a9_1", "F9B", "F9B");
  TEST_LAYOUT({a16_1}, 32, 32, "1 32 16 5 a16_1", "F16B", "FSB");
  TEST_LAYOUT({p1_256}, 32, 32, "1 256 1 11 p1_256:2700",
              "FFFFFFFF1B", "FFFFFFFF1B");
  TEST_LAYOUT({a41_1}, 32, 32, "1 32 41 7 a41_1:7", "F09B",
              "FS9B");
  TEST_LAYOUT({a105_1}, 32, 32, "1 32 105 6 a105_1", "F0009B",
              "FSSSSB");
  TEST_LAYOUT({a200_1}, 32, 32, "1 32 200 6 a200_1", "F0000008BB",
              "FSSSS008BB");

  {
    SmallVector<ASanStackVariableDescription, 10> t = {a1_1, p1_256};
    TEST_LAYOUT(t, 32, 32, "2 256 1 11 p1_256:2700 320 1 4 a1_1",
                "FFFFFFFF1M1B", "FFFFFFFF1M1B");
  }

  {
    SmallVector<ASanStackVariableDescription, 10> t = {a1_1, a16_1, a41_1};
    TEST_LAYOUT(t, 32, 32, "3 32 1 4 a1_1 96 16 5 a16_1 160 41 7 a41_1:7",
                "F1M16M09B", "F1MSMS9B");
  }
#undef VAR
#undef TEST_LAYOUT
}
