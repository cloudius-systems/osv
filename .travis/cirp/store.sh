#!/usr/bin/env bash
# MIT License

# Copyright (c) 2018-2020 by Maxim Biro <nurupo.contributions@gmail.com>

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

set -euo pipefail

. .travis/cirp/check_precondition.sh

if [ ! -z "$TRAVIS_TEST_RESULT" ] && [ "$TRAVIS_TEST_RESULT" != "0" ]; then
  echo "Build has failed, skipping publishing"
  exit 0
fi

if [ "$#" != "1" ]; then
  echo "Error: No arguments provided. Please specify a directory containing artifacts as the first argument."
  exit 1
fi

ARTIFACTS_DIR="$1"

. .travis/cirp/install.sh

ci-release-publisher --travis-api-url https://api.travis-ci.com store "$ARTIFACTS_DIR"
