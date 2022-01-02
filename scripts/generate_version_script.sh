#!/bin/bash

MACHINE=$(uname -m)
if [ "${MACHINE}" == "x86_64" ]; then
  ARCH="x64"
else
  ARCH="aarch64"
fi

VERSION_SCRIPT_FILE=$1

VERSION_SCRIPT_START=$(cat <<"EOF"
{
  global:
EOF
)

VERSION_SCRIPT_END=$(cat <<"EOF"
  local:
    *;
};
EOF
)

echo "$VERSION_SCRIPT_START" > $VERSION_SCRIPT_FILE

#Firstly output list of symbols from files common to all architectures
for file in exported_symbols/*.symbols
do
  file_name=$(basename $file)
  echo "/*------- $file_name */" >> $VERSION_SCRIPT_FILE
  cat $file | awk '// { printf("    %s;\n", $0) }' >> $VERSION_SCRIPT_FILE
done

#Secondly output list of symbols from files specific to given architecture
for file in exported_symbols/$ARCH/*.symbols
do
  file_name=$(basename $file)
  echo "/*------- $ARCH/$file_name */" >> $VERSION_SCRIPT_FILE
  cat $file | awk '// { printf("    %s;\n", $0) }' >> $VERSION_SCRIPT_FILE
done

echo "$VERSION_SCRIPT_END" >> $VERSION_SCRIPT_FILE
