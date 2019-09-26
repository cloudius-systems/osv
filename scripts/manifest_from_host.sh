#!/bin/bash

#---------------------------------------------------------------------
# Build upload manifest out of the files found on the host filesystem
# for following three scenarios:
# 1) Executable - path or name
# 2) Shared library - name or path
# 3) Directory path - take everything from the folder with option of resolving *.so files
#---------------------------------------------------------------------

NAME=$1

argv0=${0##*/}
usage() {
	cat <<-EOF
	Produce manifest referencing files on the host filesystem

	Usage: ${argv0} [options] <ELF file> | <directory path> [<subdirectory path>]

	Options:
	  -l              Look for a shared library
	  -r              Resolve all SO dependencies in directory
	  -R              Make guest root path match the host, otherwise assume '/'; applies with directory path input
	  -h              Show this help output
	  -w              Write output to ./build/last/append.manifest

	Examples:
	  ./scripts/manifest_from_host.sh ls                 # Create manifest for 'ls' executable
	  ./scripts/manifest_from_host.sh -r /some/directory # Create manifest out of the files in the directory
	  ./scripts/manifest_from_host.sh -l libz.so.1       # Create manifest for libz.so.1 library
	  ./scripts/manifest_from_host.sh -w ls && \
          ./script/build --append-manifest                   # Create manifest for 'ls' executable
	EOF
	exit ${1:-0}
}

find_library()
{
	local pattern="$1"
	local count=$(ldconfig -p | grep -P "$pattern" | grep 'x86-64' | wc -l)

	if [[ $count == 0 ]]; then
		echo "Could not find any so file matching $pattern" >&2
		return -1
	elif [[ $count > 1 ]]; then
		echo 'Found more than one alternative:' >&2
		ldconfig -p | grep -P "$pattern"
		return -1
	else
		local so_name_path=$(ldconfig -p | grep -P "$pattern" | grep 'x86-64')
		so_name=$(echo $so_name_path | grep -Po 'lib[^ ]+.+?(?= \()')
		so_path=$(echo $so_name_path | grep -Po '(?<=> )/[^ ]+')
		return 0
	fi
}

output_manifest()
{
	local so_path="$1"
	echo "# --------------------" | tee -a $OUTPUT
	echo "# Dependencies" | tee -a $OUTPUT
	echo "# --------------------" | tee -a $OUTPUT
	lddtree $so_path | grep -v "not found" | grep -v "$so_path" | grep -v 'ld-linux-x86-64' | \
		grep -Pv 'lib(gcc_s|resolv|c|m|pthread|dl|rt|stdc\+\+|aio|xenstore|crypt|selinux)\.so([\d.]+)?' | \
		sed 's/ =>/:/' | sed 's/^\s*lib/\/usr\/lib\/lib/' | sort | uniq | tee -a $OUTPUT
}

detect_elf()
{
	local file_path="$1"
	local file_desc=$(file -L $file_path)
	local elf_filter=$(echo $file_desc | grep -P 'LSB shared object|LSB.*executable' | grep 'x86-64' | wc -l)
	if [[ $elf_filter == 1 ]]; then
		local shared_object_filter=$(echo $file_desc | grep -P 'LSB shared object' | wc -l)
		if [[ $shared_object_filter == 1 ]]; then
			local pie_filter=$(echo $file_desc | grep 'interpreter' | wc -l)
			if [[ $pie_filter == 1 ]]; then
				FILE_TYPE="PIE"
				LONG_NAME="(PIE) Position Independent Executable"
			else
				FILE_TYPE="SL"
				LONG_NAME="Shared Library"
			fi
		else
			FILE_TYPE="PDE"
			LONG_NAME="Position Dependent Executable"
		fi
	else
		FILE_TYPE="NON_ELF"
		LONG_NAME="Non ELF"
	fi
}

MODE="EXEC"
RESOLVE=false
OUTPUT="/dev/null"
DEFAULT_OUTPUT_FILE="$(dirname $0)/../build/last/append.manifest"
GUEST_ROOT=true
WRITE_CMD=false

while getopts lrRwh: OPT ; do
	case ${OPT} in
	l) MODE="LIB";;
	r) RESOLVE=true;;
	R) GUEST_ROOT=false;;
	w) WRITE_CMD=true
           OUTPUT="$DEFAULT_OUTPUT_FILE";;
	h) usage;;
	?) usage 1;;
	esac
done

shift $((OPTIND - 1))
[[ -z $1 ]] && usage 1

LDDTREE_INSTALLED=$(command -v lddtree)
if [ -z "$LDDTREE_INSTALLED" ]; then
	echo "Please install lddtree which is part of pax-utils package" >&2
	exit 1
fi

NAME_OR_PATH="$1"
SUBDIRECTORY_PATH="$2"

# Check if directory and disregard LIB mode if requested
if [[ -d $NAME_OR_PATH ]]; then
	GUEST_PATH_ROOT=""
	if [[ $GUEST_ROOT == false ]]; then
		GUEST_PATH_ROOT="$(realpath $NAME_OR_PATH)"
	fi
	echo "$GUEST_PATH_ROOT/$SUBDIRECTORY_PATH**: $(realpath $NAME_OR_PATH)/$SUBDIRECTORY_PATH**" | tee $OUTPUT
	if [[ $RESOLVE == true ]]; then
		SO_FILES=$(find $NAME_OR_PATH/$SUBDIRECTORY_PATH -type f -name \*so)
		echo "# --------------------" | tee -a $OUTPUT
		echo "# Dependencies" | tee -a $OUTPUT
		echo "# --------------------" | tee -a $OUTPUT
		lddtree $SO_FILES | grep -v "not found" | grep -v "$NAME_OR_PATH/$SUBDIRECTORY_PATH" | grep -v 'ld-linux-x86-64' | \
			grep -Pv 'lib(gcc_s|resolv|c|m|pthread|dl|rt|stdc\+\+|aio|xenstore|crypt|selinux)\.so([\d.]+)?' | \
			sed 's/ =>/:/' | sed 's/^\s*lib/\/usr\/lib\/lib/' | sort | uniq | tee -a $OUTPUT
	fi
	exit 0
fi

# Check if file exists
if [[ -f $NAME_OR_PATH ]]; then
	# Detect if NAME_PATH point to an ELF executable or library
	detect_elf $NAME_OR_PATH
	if [[ $FILE_TYPE != "NON_ELF" ]]; then
		echo "# $LONG_NAME" | tee $OUTPUT
		NAME=$(basename $NAME_OR_PATH)
		# Detect if ELF is an executable
		if [[ $FILE_TYPE == "SL" ]]; then
			# Library
			echo "/usr/lib/$NAME: $(realpath $NAME_OR_PATH)" | tee -a $OUTPUT
		else
			echo "/$NAME: $(realpath $NAME_OR_PATH)" | tee -a $OUTPUT
			if [[ $WRITE_CMD == true ]]; then
				printf "/$NAME --help" | tee "$(dirname $0)/../build/last/append_cmdline"
			fi
		fi
		REAL_PATH=$(realpath $NAME_OR_PATH)
		output_manifest "$REAL_PATH"
	else
		echo "The $NAME_OR_PATH is not ELF" >&2
		exit 1
	fi
else
	# Do not assume ELF shared library unless mode specifies it
	if [[ $MODE == "LIB" ]]; then
		find_library "$NAME_OR_PATH"
		if [[ $? == 0 ]]; then
			echo "# Shared library" | tee $OUTPUT
			echo "/usr/lib/$so_name: $so_path" | tee -a $OUTPUT
			output_manifest $so_path
		else
			exit 1
		fi
	else
		APP_PATH=$(which $NAME_OR_PATH)
		if [[ $? == 0 ]]; then
			FULL_PATH=$(realpath $APP_PATH)
			detect_elf $FULL_PATH
			if [[ $FILE_TYPE == "NON_ELF" ]]; then
				echo "The file $FULL_PATH is not an ELF" >&2
				exit 1
			else
				echo "# $LONG_NAME" | tee $OUTPUT
				echo "/$NAME_OR_PATH: $FULL_PATH" | tee -a $OUTPUT
				output_manifest $FULL_PATH
				if [[ $WRITE_CMD == true ]]; then
					printf "/$NAME_OR_PATH --help" | tee "$(dirname $0)/../build/last/append_cmdline"
				fi
			fi
		else
			echo "Failed to find '$NAME_OR_PATH'!" >&2
			exit 1
		fi
	fi
fi

echo "# --------------------" | tee -a $OUTPUT
exit 0
