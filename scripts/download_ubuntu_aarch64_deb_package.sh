#!/bin/bash

package_directory="$1"
package="$2"
out_dir=$3

arch=arm64

letter=${package_directory:0:1}

ports_main_repo_url="http://ports.ubuntu.com/pool/main/$letter/$package_directory/"
ports_universe_repo_url="http://ports.ubuntu.com/pool/universe/$letter/$package_directory/"

package_file_name=$(wget -t 1 -qO- $ports_main_repo_url | grep -Po "href=\"[^\"]*\"" | grep ${arch} | grep -Po "${package}[^\"]*" | tail -n 1)
if [[ "${package_file_name}" == "" ]]; then
   package_file_name=$(wget -t 1 -qO- $ports_universe_repo_url | grep -Po "href=\"[^\"]*\"" | grep ${arch} | grep -Po "${package}[^\"]*" | tail -n 1)
   package_url="${ports_universe_repo_url}${package_file_name}"
else
   package_url="${ports_main_repo_url}${package_file_name}"
fi
echo $package_url

if [[ "${package_file_name}" == "" ]]; then
  echo "Could not find $package under $ports_main_repo_url nor $ports_universe_repo_url!"
  exit 1
fi

mkdir -p ${out_dir}/upstream
wget -c -O ${out_dir}/upstream/${package_file_name} $package_url
mkdir -p ${out_dir}/install
dpkg --extract ${out_dir}/upstream/${package_file_name} ${out_dir}/install
