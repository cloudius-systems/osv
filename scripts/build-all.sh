#/bin/sh -e

cd external/libunwind
autoreconf -i
sh config.sh
make
cp ./src/.libs/libunwind.a ../..
cd ../..
cd external/glibc-testsuite
make
cd ../..
make

