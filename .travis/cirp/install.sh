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
# Install verifying the hash

# Verifying PGP signature on CI is error-prone, keyservers often fail to return
# the key and even if they do, `gpg --verify` returns success with a revoked
# or expired key. Thus it's probably better to verify the signature yourself,
# on your local machine, and then rely on the hash on the CI.

# Set the variables below for the version of ci_release_publisher you would like
# to use. The set values are provided as an example and are likely very out of
# date.

#VERSION="0.1.0rc1"
#FILENAME="ci_release_publisher-$VERSION-py3-none-any.whl"
#HASH="5a7f0ad6ccfb6017974db42fb1ecfe8b3f9cc1c16ac68107a94979252baa16e3"

# Get Python >=3.5
if [ "$TRAVIS_OS_NAME" == "osx" ]; then
  brew update

  # Upgrade Python 2 to Python 3
  brew upgrade python || true

  # Print python versions
  python --version || true
  python3 --version || true
  pyenv versions || true

  cd .
  cd "$(mktemp -d)"
  virtualenv env -p python3
  set +u
  source env/bin/activate
  set -u
  cd -

  # make sha256sum available
  export PATH="/usr/local/opt/coreutils/libexec/gnubin:$PATH"
elif [ "$TRAVIS_OS_NAME" == "linux" ]; then
  # Print python versions
  python --version || true
  python3 --version || true
  pyenv versions || true

  # Install Python >=3.5 that has a non-zero patch version
  # (we assume the zero patch versions to be potentially buggier than desired)
  pyenv global $(pyenv versions | grep -o ' 3\.[5-99]\.[1-99]*' | tail -n1)
fi

python3 -m pip install --user --upgrade pip
python3 -m pip install --user setuptools

# Don't install again if already installed.
# OSX keeps re-installing it tough, as it uses a temp per-script virtualenv.
if ! python3 -m pip list --format=columns | grep '^ci-release-publisher '; then
  python3 -m pip install --user https://files.pythonhosted.org/packages/49/20/2631e993daa85b35c8390e8124570b0321825e6a77e566492b4637566983/ci_release_publisher-0.3.0.tar.gz
  # Downgrade to specific version of urllib3 to avoid the 'AttributeError: type object 'Retry' has no attribute 'DEFAULT_METHOD_WHITELIST'
  python3 -m pip uninstall -y urllib3
  python3 -m pip install urllib3==1.26.15
fi
