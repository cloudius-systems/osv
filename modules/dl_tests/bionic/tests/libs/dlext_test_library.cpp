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

class A {
public:
  virtual int getRandomNumber() {
    return 4;  // chosen by fair dice roll.
               // guaranteed to be random.
  }

  virtual ~A() {}
};

A a;

// nested macros to make it easy to define a large amount of read-only data
// which will require relocation.
#define A_16 &a, &a, &a, &a, &a, &a, &a, &a, &a, &a, &a, &a, &a, &a, &a, &a,
#define A_128 A_16 A_16 A_16 A_16 A_16 A_16 A_16 A_16
#define A_1024 A_128 A_128 A_128 A_128 A_128 A_128 A_128 A_128

extern "C" A* const lots_of_relro[] = {
  A_1024 A_1024 A_1024 A_1024 A_1024 A_1024 A_1024 A_1024
};

extern "C" int getRandomNumber() {
  // access the relro section (twice, in fact, once for the pointer, and once
  // for the vtable of A) to check it's actually there.
  return lots_of_relro[0]->getRandomNumber();
}
