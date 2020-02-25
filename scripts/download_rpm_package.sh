#!/bin/bash

package="$1"
release=$2
out_dir=$3

arch=aarch64

letter=${package:0:1}

main_repo_url="http://mirrors.kernel.org/fedora/releases/$release/Everything/$arch/os/Packages/$letter/"
version=$(wget -t 1 -qO- $main_repo_url | grep "${package}-[0-9].*$arch" | grep -Po ">.*<" | sed -e "s/>${package}-\(.*\)\.$arch\.rpm</\1/g" | tail -l)

archive_repo_url="http://archives.fedoraproject.org/pub/archive/fedora/linux/releases/$release/Everything/$arch/os/Packages/$letter/"
if [[ "${version}" != "" ]]; then
  file_name="${package}-${version}.aarch64.rpm"
  full_url="${main_repo_url}${file_name}"
else
  version=$(wget -t 1 -qO- $archive_repo_url | grep "${package}-[0-9].*$arch" | grep -Po "<a href.*>" | sed -e "s/<a href\=\"${package}-\(.*\)\.$arch\.rpm\".*/\1/g" | tail -l)
  if [[ "${version}" != "" ]]; then
    file_name="${package}-${version}.aarch64.rpm"
    full_url="${archive_repo_url}${file_name}"
  fi
fi

if [[ "${version}" == "" ]]; then
  echo "Could not find $package under $main_repo_url nor $archive_repo_url!"
  exit 1
fi

mkdir -p ${out_dir}/upstream
wget -c -O ${out_dir}/upstream/${file_name} ${full_url}
mkdir -p ${out_dir}/install
rpm2cpio ${out_dir}/upstream/${file_name} | (cd ${out_dir}/install && cpio -id)
