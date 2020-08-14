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

#include <gtest/gtest.h>

#include <ctype.h>

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
      EXPECT_TRUE(isalnum(i)) << i;
    } else {
      EXPECT_FALSE(isalnum(i)) << i;
    }
  }
}

TEST(ctype, isalnum_l) {
  for (int i = kMin; i < kMax; ++i) {
    if ((i >= '0' && i <= '9') ||
        (i >= 'A' && i <= 'Z') ||
        (i >= 'a' && i <= 'z')) {
      EXPECT_TRUE(isalnum_l(i, LC_GLOBAL_LOCALE)) << i;
    } else {
      EXPECT_FALSE(isalnum_l(i, LC_GLOBAL_LOCALE)) << i;
    }
  }
}

TEST(ctype, isalpha) {
  for (int i = kMin; i < kMax; ++i) {
    if ((i >= 'A' && i <= 'Z') ||
        (i >= 'a' && i <= 'z')) {
      EXPECT_TRUE(isalpha(i)) << i;
    } else {
      EXPECT_FALSE(isalpha(i)) << i;
    }
  }
}

TEST(ctype, isalpha_l) {
  for (int i = kMin; i < kMax; ++i) {
    if ((i >= 'A' && i <= 'Z') ||
        (i >= 'a' && i <= 'z')) {
      EXPECT_TRUE(isalpha_l(i, LC_GLOBAL_LOCALE)) << i;
    } else {
      EXPECT_FALSE(isalpha_l(i, LC_GLOBAL_LOCALE)) << i;
    }
  }
}

TEST(ctype, isascii) {
  for (int i = kMin; i < kMax; ++i) {
    if (i >= 0 && i <= 0x7f) {
      EXPECT_TRUE(isascii(i)) << i;
    } else {
      EXPECT_FALSE(isascii(i)) << i;
    }
  }
}

TEST(ctype, isblank) {
  for (int i = kMin; i < kMax; ++i) {
    if (i == '\t' || i == ' ') {
      EXPECT_TRUE(isblank(i)) << i;
    } else {
      EXPECT_FALSE(isblank(i)) << i;
    }
  }
}

TEST(ctype, isblank_l) {
  for (int i = kMin; i < kMax; ++i) {
    if (i == '\t' || i == ' ') {
      EXPECT_TRUE(isblank_l(i, LC_GLOBAL_LOCALE)) << i;
    } else {
      EXPECT_FALSE(isblank_l(i, LC_GLOBAL_LOCALE)) << i;
    }
  }
}

TEST(ctype, iscntrl) {
  for (int i = kMin; i < kMax; ++i) {
    if ((i >= 0 && i < ' ') || i == 0x7f) {
      EXPECT_TRUE(iscntrl(i)) << i;
    } else {
      EXPECT_FALSE(iscntrl(i)) << i;
    }
  }
}

TEST(ctype, iscntrl_l) {
  for (int i = kMin; i < kMax; ++i) {
    if ((i >= 0 && i < ' ') || i == 0x7f) {
      EXPECT_TRUE(iscntrl_l(i, LC_GLOBAL_LOCALE)) << i;
    } else {
      EXPECT_FALSE(iscntrl_l(i, LC_GLOBAL_LOCALE)) << i;
    }
  }
}

TEST(ctype, isdigit) {
  for (int i = kMin; i < kMax; ++i) {
    if (i >= '0' && i <= '9') {
      EXPECT_TRUE(isdigit(i)) << i;
    } else {
      EXPECT_FALSE(isdigit(i)) << i;
    }
  }
}

TEST(ctype, isdigit_l) {
  for (int i = kMin; i < kMax; ++i) {
    if (i >= '0' && i <= '9') {
      EXPECT_TRUE(isdigit_l(i, LC_GLOBAL_LOCALE)) << i;
    } else {
      EXPECT_FALSE(isdigit_l(i, LC_GLOBAL_LOCALE)) << i;
    }
  }
}

TEST(ctype, isgraph) {
  for (int i = kMin; i < kMax; ++i) {
    if (i >= '!' && i <= '~') {
      EXPECT_TRUE(isgraph(i)) << i;
    } else {
      EXPECT_FALSE(isgraph(i)) << i;
    }
  }
}

TEST(ctype, isgraph_l) {
  for (int i = kMin; i < kMax; ++i) {
    if (i >= '!' && i <= '~') {
      EXPECT_TRUE(isgraph_l(i, LC_GLOBAL_LOCALE)) << i;
    } else {
      EXPECT_FALSE(isgraph_l(i, LC_GLOBAL_LOCALE)) << i;
    }
  }
}

TEST(ctype, islower) {
  for (int i = kMin; i < kMax; ++i) {
    if (i >= 'a' && i <= 'z') {
      EXPECT_TRUE(islower(i)) << i;
    } else {
      EXPECT_FALSE(islower(i)) << i;
    }
  }
}

TEST(ctype, islower_l) {
  for (int i = kMin; i < kMax; ++i) {
    if (i >= 'a' && i <= 'z') {
      EXPECT_TRUE(islower_l(i, LC_GLOBAL_LOCALE)) << i;
    } else {
      EXPECT_FALSE(islower_l(i, LC_GLOBAL_LOCALE)) << i;
    }
  }
}

TEST(ctype, isprint) {
  for (int i = kMin; i < kMax; ++i) {
    if (i >= ' ' && i <= '~') {
      EXPECT_TRUE(isprint(i)) << i;
    } else {
      EXPECT_FALSE(isprint(i)) << i;
    }
  }
}

TEST(ctype, isprint_l) {
  for (int i = kMin; i < kMax; ++i) {
    if (i >= ' ' && i <= '~') {
      EXPECT_TRUE(isprint_l(i, LC_GLOBAL_LOCALE)) << i;
    } else {
      EXPECT_FALSE(isprint_l(i, LC_GLOBAL_LOCALE)) << i;
    }
  }
}

TEST(ctype, ispunct) {
  for (int i = kMin; i < kMax; ++i) {
    if ((i >= '!' && i <= '/') ||
        (i >= ':' && i <= '@') ||
        (i >= '[' && i <= '`') ||
        (i >= '{' && i <= '~')) {
      EXPECT_TRUE(ispunct(i)) << i;
    } else {
      EXPECT_FALSE(ispunct(i)) << i;
    }
  }
}

TEST(ctype, ispunct_l) {
  for (int i = kMin; i < kMax; ++i) {
    if ((i >= '!' && i <= '/') ||
        (i >= ':' && i <= '@') ||
        (i >= '[' && i <= '`') ||
        (i >= '{' && i <= '~')) {
      EXPECT_TRUE(ispunct_l(i, LC_GLOBAL_LOCALE)) << i;
    } else {
      EXPECT_FALSE(ispunct_l(i, LC_GLOBAL_LOCALE)) << i;
    }
  }
}

TEST(ctype, isspace) {
  for (int i = kMin; i < kMax; ++i) {
    if ((i >= '\t' && i <= '\r') || i == ' ') {
      EXPECT_TRUE(isspace(i)) << i;
    } else {
      EXPECT_FALSE(isspace(i)) << i;
    }
  }
}

TEST(ctype, isspace_l) {
  for (int i = kMin; i < kMax; ++i) {
    if ((i >= '\t' && i <= '\r') || i == ' ') {
      EXPECT_TRUE(isspace_l(i, LC_GLOBAL_LOCALE)) << i;
    } else {
      EXPECT_FALSE(isspace_l(i, LC_GLOBAL_LOCALE)) << i;
    }
  }
}

TEST(ctype, isupper) {
  for (int i = kMin; i < kMax; ++i) {
    if (i >= 'A' && i <= 'Z') {
      EXPECT_TRUE(isupper(i)) << i;
    } else {
      EXPECT_FALSE(isupper(i)) << i;
    }
  }
}

TEST(ctype, isupper_l) {
  for (int i = kMin; i < kMax; ++i) {
    if (i >= 'A' && i <= 'Z') {
      EXPECT_TRUE(isupper_l(i, LC_GLOBAL_LOCALE)) << i;
    } else {
      EXPECT_FALSE(isupper_l(i, LC_GLOBAL_LOCALE)) << i;
    }
  }
}

TEST(ctype, isxdigit) {
  for (int i = kMin; i < kMax; ++i) {
    if ((i >= '0' && i <= '9') ||
        (i >= 'A' && i <= 'F') ||
        (i >= 'a' && i <= 'f')) {
      EXPECT_TRUE(isxdigit(i)) << i;
    } else {
      EXPECT_FALSE(isxdigit(i)) << i;
    }
  }
}

TEST(ctype, isxdigit_l) {
  for (int i = kMin; i < kMax; ++i) {
    if ((i >= '0' && i <= '9') ||
        (i >= 'A' && i <= 'F') ||
        (i >= 'a' && i <= 'f')) {
      EXPECT_TRUE(isxdigit_l(i, LC_GLOBAL_LOCALE)) << i;
    } else {
      EXPECT_FALSE(isxdigit_l(i, LC_GLOBAL_LOCALE)) << i;
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
