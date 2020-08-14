/*
 * Copyright (C) 2014 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Copyright (C) 2020 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// This file is a verbatim copy of tests/ctype_test.cpp from the bionic project
// (https://android.googlesource.com/platform/bionic as of commit: 9c6d60d073db079a87fbeb5de3e72ac12838a480)
// PLUS some minor tweaks (mostly macros) that adapt it to run with boost unit framework
// instead of Google's test framework

//#include <gtest/gtest.h>
#define BOOST_TEST_MODULE tst-ctype

#include <boost/test/unit_test.hpp>
namespace utf = boost::unit_test;

#include <ctype.h>

#define TEST(MODULE_NAME,TEST_NAME) BOOST_AUTO_TEST_CASE(MODULE_NAME##TEST_NAME)
#define EXPECT_TRUE(EXP) BOOST_REQUIRE_MESSAGE(EXP, "Failed for " << i)
#define EXPECT_FALSE(EXP) BOOST_REQUIRE_MESSAGE(!(EXP), "Failed for " << i)
#define EXPECT_EQ(EXP1,EXP2) BOOST_CHECK_EQUAL(EXP1,EXP2)

// We test from -1 (EOF) to 0xff, because that's the range for which behavior
// is actually defined. (It's explicitly undefined below or above that.) Most
// of our routines are no longer table-based and behave correctly for the
// entire int range, but that's not true of other C libraries that we might
// want to compare against, nor of our isalnum(3) and ispunt(3).
static constexpr int kMin = -1;
static constexpr int kMax = 256;

TEST(ctype, isalnum) {
  for (int i = kMin; i < kMax; ++i) {
    if ((i >= '0' && i <= '9') ||
        (i >= 'A' && i <= 'Z') ||
        (i >= 'a' && i <= 'z')) {
      EXPECT_TRUE(isalnum(i));
    } else {
      EXPECT_FALSE(isalnum(i));
    }
  }
}

TEST(ctype, isalnum_l) {
  for (int i = kMin; i < kMax; ++i) {
    if ((i >= '0' && i <= '9') ||
        (i >= 'A' && i <= 'Z') ||
        (i >= 'a' && i <= 'z')) {
      EXPECT_TRUE(isalnum_l(i, LC_GLOBAL_LOCALE));
    } else {
      EXPECT_FALSE(isalnum_l(i, LC_GLOBAL_LOCALE));
    }
  }
}

TEST(ctype, isalpha) {
  for (int i = kMin; i < kMax; ++i) {
    if ((i >= 'A' && i <= 'Z') ||
        (i >= 'a' && i <= 'z')) {
      EXPECT_TRUE(isalpha(i));
    } else {
      EXPECT_FALSE(isalpha(i));
    }
  }
}

TEST(ctype, isalpha_l) {
  for (int i = kMin; i < kMax; ++i) {
    if ((i >= 'A' && i <= 'Z') ||
        (i >= 'a' && i <= 'z')) {
      EXPECT_TRUE(isalpha_l(i, LC_GLOBAL_LOCALE));
    } else {
      EXPECT_FALSE(isalpha_l(i, LC_GLOBAL_LOCALE));
    }
  }
}

TEST(ctype, isascii) {
  for (int i = kMin; i < kMax; ++i) {
    if (i >= 0 && i <= 0x7f) {
      EXPECT_TRUE(isascii(i));
    } else {
      EXPECT_FALSE(isascii(i));
    }
  }
}

TEST(ctype, isblank) {
  for (int i = kMin; i < kMax; ++i) {
    if (i == '\t' || i == ' ') {
      EXPECT_TRUE(isblank(i));
    } else {
      EXPECT_FALSE(isblank(i));
    }
  }
}

TEST(ctype, isblank_l) {
  for (int i = kMin; i < kMax; ++i) {
    if (i == '\t' || i == ' ') {
      EXPECT_TRUE(isblank_l(i, LC_GLOBAL_LOCALE));
    } else {
      EXPECT_FALSE(isblank_l(i, LC_GLOBAL_LOCALE));
    }
  }
}

TEST(ctype, iscntrl) {
  for (int i = kMin; i < kMax; ++i) {
    if ((i >= 0 && i < ' ') || i == 0x7f) {
      EXPECT_TRUE(iscntrl(i));
    } else {
      EXPECT_FALSE(iscntrl(i));
    }
  }
}

TEST(ctype, iscntrl_l) {
  for (int i = kMin; i < kMax; ++i) {
    if ((i >= 0 && i < ' ') || i == 0x7f) {
      EXPECT_TRUE(iscntrl_l(i, LC_GLOBAL_LOCALE));
    } else {
      EXPECT_FALSE(iscntrl_l(i, LC_GLOBAL_LOCALE));
    }
  }
}

TEST(ctype, isdigit) {
  for (int i = kMin; i < kMax; ++i) {
    if (i >= '0' && i <= '9') {
      EXPECT_TRUE(isdigit(i));
    } else {
      EXPECT_FALSE(isdigit(i));
    }
  }
}

TEST(ctype, isdigit_l) {
  for (int i = kMin; i < kMax; ++i) {
    if (i >= '0' && i <= '9') {
      EXPECT_TRUE(isdigit_l(i, LC_GLOBAL_LOCALE));
    } else {
      EXPECT_FALSE(isdigit_l(i, LC_GLOBAL_LOCALE));
    }
  }
}

TEST(ctype, isgraph) {
  for (int i = kMin; i < kMax; ++i) {
    if (i >= '!' && i <= '~') {
      EXPECT_TRUE(isgraph(i));
    } else {
      EXPECT_FALSE(isgraph(i));
    }
  }
}

TEST(ctype, isgraph_l) {
  for (int i = kMin; i < kMax; ++i) {
    if (i >= '!' && i <= '~') {
      EXPECT_TRUE(isgraph_l(i, LC_GLOBAL_LOCALE));
    } else {
      EXPECT_FALSE(isgraph_l(i, LC_GLOBAL_LOCALE));
    }
  }
}

TEST(ctype, islower) {
  for (int i = kMin; i < kMax; ++i) {
    if (i >= 'a' && i <= 'z') {
      EXPECT_TRUE(islower(i));
    } else {
      EXPECT_FALSE(islower(i));
    }
  }
}

TEST(ctype, islower_l) {
  for (int i = kMin; i < kMax; ++i) {
    if (i >= 'a' && i <= 'z') {
      EXPECT_TRUE(islower_l(i, LC_GLOBAL_LOCALE));
    } else {
      EXPECT_FALSE(islower_l(i, LC_GLOBAL_LOCALE));
    }
  }
}

TEST(ctype, isprint) {
  for (int i = kMin; i < kMax; ++i) {
    if (i >= ' ' && i <= '~') {
      EXPECT_TRUE(isprint(i));
    } else {
      EXPECT_FALSE(isprint(i));
    }
  }
}

TEST(ctype, isprint_l) {
  for (int i = kMin; i < kMax; ++i) {
    if (i >= ' ' && i <= '~') {
      EXPECT_TRUE(isprint_l(i, LC_GLOBAL_LOCALE));
    } else {
      EXPECT_FALSE(isprint_l(i, LC_GLOBAL_LOCALE));
    }
  }
}

TEST(ctype, ispunct) {
  for (int i = kMin; i < kMax; ++i) {
    if ((i >= '!' && i <= '/') ||
        (i >= ':' && i <= '@') ||
        (i >= '[' && i <= '`') ||
        (i >= '{' && i <= '~')) {
      EXPECT_TRUE(ispunct(i));
    } else {
      EXPECT_FALSE(ispunct(i));
    }
  }
}

TEST(ctype, ispunct_l) {
  for (int i = kMin; i < kMax; ++i) {
    if ((i >= '!' && i <= '/') ||
        (i >= ':' && i <= '@') ||
        (i >= '[' && i <= '`') ||
        (i >= '{' && i <= '~')) {
      EXPECT_TRUE(ispunct_l(i, LC_GLOBAL_LOCALE));
    } else {
      EXPECT_FALSE(ispunct_l(i, LC_GLOBAL_LOCALE));
    }
  }
}

TEST(ctype, isspace) {
  for (int i = kMin; i < kMax; ++i) {
    if ((i >= '\t' && i <= '\r') || i == ' ') {
      EXPECT_TRUE(isspace(i));
    } else {
      EXPECT_FALSE(isspace(i));
    }
  }
}

TEST(ctype, isspace_l) {
  for (int i = kMin; i < kMax; ++i) {
    if ((i >= '\t' && i <= '\r') || i == ' ') {
      EXPECT_TRUE(isspace_l(i, LC_GLOBAL_LOCALE));
    } else {
      EXPECT_FALSE(isspace_l(i, LC_GLOBAL_LOCALE));
    }
  }
}

TEST(ctype, isupper) {
  for (int i = kMin; i < kMax; ++i) {
    if (i >= 'A' && i <= 'Z') {
      EXPECT_TRUE(isupper(i));
    } else {
      EXPECT_FALSE(isupper(i));
    }
  }
}

TEST(ctype, isupper_l) {
  for (int i = kMin; i < kMax; ++i) {
    if (i >= 'A' && i <= 'Z') {
      EXPECT_TRUE(isupper_l(i, LC_GLOBAL_LOCALE));
    } else {
      EXPECT_FALSE(isupper_l(i, LC_GLOBAL_LOCALE));
    }
  }
}

TEST(ctype, isxdigit) {
  for (int i = kMin; i < kMax; ++i) {
    if ((i >= '0' && i <= '9') ||
        (i >= 'A' && i <= 'F') ||
        (i >= 'a' && i <= 'f')) {
      EXPECT_TRUE(isxdigit(i));
    } else {
      EXPECT_FALSE(isxdigit(i));
    }
  }
}

TEST(ctype, isxdigit_l) {
  for (int i = kMin; i < kMax; ++i) {
    if ((i >= '0' && i <= '9') ||
        (i >= 'A' && i <= 'F') ||
        (i >= 'a' && i <= 'f')) {
      EXPECT_TRUE(isxdigit_l(i, LC_GLOBAL_LOCALE));
    } else {
      EXPECT_FALSE(isxdigit_l(i, LC_GLOBAL_LOCALE));
    }
  }
}

TEST(ctype, toascii) {
  EXPECT_EQ('a', toascii('a'));
  EXPECT_EQ('a', toascii(0x80 | 'a'));
}

TEST(ctype, tolower) {
  EXPECT_EQ('!', tolower('!'));
  EXPECT_EQ('a', tolower('a'));
  EXPECT_EQ('a', tolower('A'));
}

TEST(ctype, tolower_l) {
  EXPECT_EQ('!', tolower_l('!', LC_GLOBAL_LOCALE));
  EXPECT_EQ('a', tolower_l('a', LC_GLOBAL_LOCALE));
  EXPECT_EQ('a', tolower_l('A', LC_GLOBAL_LOCALE));
}

TEST(ctype, _tolower) {
  // _tolower may mangle characters for which isupper is false.
  EXPECT_EQ('a', _tolower('A'));
}

TEST(ctype, toupper) {
  EXPECT_EQ('!', toupper('!'));
  EXPECT_EQ('A', toupper('a'));
  EXPECT_EQ('A', toupper('A'));
}

TEST(ctype, toupper_l) {
  EXPECT_EQ('!', toupper_l('!', LC_GLOBAL_LOCALE));
  EXPECT_EQ('A', toupper_l('a', LC_GLOBAL_LOCALE));
  EXPECT_EQ('A', toupper_l('A', LC_GLOBAL_LOCALE));
}

TEST(ctype, _toupper) {
  // _toupper may mangle characters for which islower is false.
  EXPECT_EQ('A', _toupper('a'));
}
