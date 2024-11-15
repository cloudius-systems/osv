#!/bin/bash

if [[ "$1" == "--help" || "$1" == "-h" ]]; then
  cat <<-EOF
Produce version script file under build/last/app_version_script intended
to build custom kernel exporting only symbols listed in this file.

The script reads default user manifest file - build/last/usr.manifest
to identify all ELF files - executables and shared libraries - and
extract names of all symbols required to be exported by OSv kernel.

You can override location of the source manifest and pass its path
as 1st argument.

Usage: ${0} [<manifest_file_path>]

NOTE: Given that some executables and libraries may dynamically resolve
symbols using dlsym(), this script would miss to identify those. In this
case one would have to manually add those symbols to build/last/app_version_script.
EOF
  exit 0
fi

MACHINE=$(uname -m)
if [ "${MACHINE}" == "x86_64" ]; then
  ARCH="x64"
else
  ARCH="aarch64"
fi

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

BUILD_DIR=$(dirname $0)/../build/last
VERSION_SCRIPT_FILE=$(dirname $0)/../build/last/app_version_script

ALL_SYMBOLS_FILE=$BUILD_DIR/all.symbols
if [[ ! -f $ALL_SYMBOLS_FILE ]]; then
  echo "Could not find $ALL_SYMBOLS_FILE. Please run build first!"
  exit 1
fi

USR_MANIFEST=$1
if [[ "$USR_MANIFEST" == "" ]]; then
  USR_MANIFEST=$BUILD_DIR/usr.manifest
fi
if [[ ! -f $USR_MANIFEST ]]; then
  echo "Could not find $USR_MANIFEST. Please run build first!"
  exit 1
fi

MANIFEST_FILES=$BUILD_DIR/usr.manifest.files
echo "Extracting list of files on host from $USR_MANIFEST"
scripts/list_manifest_files.py -m $USR_MANIFEST > $MANIFEST_FILES

extract_symbols_from_elf()
{
  local ELF_PATH=$1
  echo "/*------- $ELF_PATH */"
  objdump -wT ${ELF_PATH} | grep UND | cut -c 62- | \
  sort -d | uniq | tr -d " " | comm - ${ALL_SYMBOLS_FILE} -12 | \
  awk '// { printf("    %s;\n", $0) }' | tee /tmp/generate_app_version_script_symbols
  if [[ $(grep dlsym /tmp/generate_app_version_script_symbols) != "" ]]; then
     echo "WARNING: the $ELF_PATH may use dlsym() to dynamically reference symbols!" 1>&2
  fi
}

echo "Writing to $VERSION_SCRIPT_FILE ..."
echo "$VERSION_SCRIPT_START" > $VERSION_SCRIPT_FILE

cat $MANIFEST_FILES | xargs file | grep "ELF 64-bit" | cut --delimiter=: -f 1 | \
while read file; do extract_symbols_from_elf "$file"; done >> $VERSION_SCRIPT_FILE

echo "$VERSION_SCRIPT_END" >> $VERSION_SCRIPT_FILE
