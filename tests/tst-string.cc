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

// This test is based on tests/string_test.cpp from the bionic project
// (https://android.googlesource.com/platform/bionic as of commit: 9c6d60d073db079a87fbeb5de3e72ac12838a480)
// PLUS some minor tweaks (mostly macros) that adapt it to run with boost unit framework
// instead of Google's test framework

#define BOOST_TEST_MODULE tst-string

#include <boost/test/unit_test.hpp>
namespace utf = boost::unit_test;

#define TEST(MODULE_NAME,TEST_NAME) BOOST_AUTO_TEST_CASE(MODULE_NAME##TEST_NAME)
#define ASSERT_TRUE(EXP) BOOST_REQUIRE(EXP)
#define ASSERT_GT(EXP1,EXP2) BOOST_REQUIRE((EXP1)>(EXP2))

TEST(STRING_TEST, strxfrm_smoke) {
  locale_t l(newlocale(LC_ALL, "C.UTF-8", nullptr));
  const char* src1 = "aab";
  char dst1[16] = {};
  ASSERT_GT(strxfrm(dst1, src1, sizeof(dst1)), 0U);
  ASSERT_GT(strxfrm_l(dst1, src1, sizeof(dst1), l), 0U);
  const char* src2 = "aac";
  char dst2[16] = {};
  ASSERT_GT(strxfrm(dst2, src2, sizeof(dst2)), 0U);
  ASSERT_GT(strxfrm_l(dst2, src2, sizeof(dst2), l), 0U);
  ASSERT_TRUE(strcmp(dst1, dst2) < 0);
  freelocale(l);
}

TEST(STRING_TEST, strcoll_smoke) {
  locale_t l(newlocale(LC_ALL, "C.UTF-8", nullptr));
  ASSERT_TRUE(strcoll("aab", "aac") < 0);
  ASSERT_TRUE(strcoll_l("aab", "aac", l) < 0);
  ASSERT_TRUE(strcoll("aab", "aab") == 0);
  ASSERT_TRUE(strcoll_l("aab", "aab", l) == 0);
  ASSERT_TRUE(strcoll("aac", "aab") > 0);
  ASSERT_TRUE(strcoll_l("aac", "aab", l) > 0);
  freelocale(l);
}
