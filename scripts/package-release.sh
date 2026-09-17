#!/bin/sh

set -eu

usage() {
	echo "usage: $0 macos|linux" >&2
	exit 2
}

[ "$#" -eq 1 ] || usage
package_platform=$1

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$script_dir/.." && pwd)
cd "$repo_dir"

version=$(sed -n 's/^#define VERSION "\([^"]*\)"$/\1/p' src/version.h)
if [ -z "$version" ] || ! printf '%s\n' "$version" |
	grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+([.+-][0-9A-Za-z.-]+)?$'; then
	echo "error: could not read a safe release version from src/version.h" >&2
	exit 1
fi

host_os=$(uname -s)
host_arch=$(uname -m)
case "$package_platform" in
	macos)
		[ "$host_os" = Darwin ] || {
			echo "error: package-macos must run on macOS" >&2
			exit 1
		}
		[ "$host_arch" = arm64 ] || {
			echo "error: package-macos requires an arm64 host" >&2
			exit 1
		}
		artifact_platform=macos-arm64
		readme_template=contrib/release/README.macos-arm64.txt
		;;
	linux)
		[ "$host_os" = Linux ] || {
			echo "error: package-linux must run on Linux" >&2
			exit 1
		}
		[ "$host_arch" = x86_64 ] || {
			echo "error: package-linux requires an x86_64 host" >&2
			exit 1
		}
		artifact_platform=linux-x86_64
		readme_template=contrib/release/README.linux-x86_64.txt
		;;
	*)
		usage
		;;
esac

make_command=${MAKE:-make}
"$make_command" clean
"$make_command"

expected_version="signalbox $version"
actual_version=$(./signalbox --version)
if [ "$actual_version" != "$expected_version" ]; then
	echo "error: expected '$expected_version', got '$actual_version'" >&2
	exit 1
fi

file_output=$(file ./signalbox)
printf '%s\n' "$file_output"
case "$package_platform:$file_output" in
	macos:*Mach-O*arm64*) ;;
	linux:*ELF*x86-64*) ;;
	*)
		echo "error: built executable does not match $artifact_platform" >&2
		exit 1
		;;
esac

audit_file=$(mktemp "${TMPDIR:-/tmp}/signalbox-dependencies.XXXXXX")
trap 'rm -f "$audit_file"' EXIT HUP INT TERM

if [ "$package_platform" = macos ]; then
	otool -L ./signalbox | tee "$audit_file"
	awk '
		NR == 1 { next }
		{
			dependency = $1
			if (dependency ~ "^/usr/lib/" ||
				dependency ~ "^/System/Library/" ||
				dependency ~ "^/opt/homebrew/opt/(ffmpeg|libgcrypt|json-c|libao)/") {
				next
			}
			printf "error: unexpected macOS dependency: %s\n", dependency > "/dev/stderr"
			bad = 1
		}
		END { exit bad }
	' "$audit_file"
else
	ldd ./signalbox | tee "$audit_file"
	if grep -F 'not found' "$audit_file" >/dev/null; then
		echo "error: Linux executable has unresolved dependencies" >&2
		exit 1
	fi
	if grep -F "$repo_dir/" "$audit_file" >/dev/null ||
		grep -E '(^|[[:space:]]=>[[:space:]])(/tmp|/private/tmp)/' "$audit_file" >/dev/null; then
		echo "error: Linux executable resolves a dependency from a build or temporary directory" >&2
		exit 1
	fi
fi

package_name="signalbox-$version-$artifact_platform"
dist_dir=$repo_dir/dist
stage_dir=$dist_dir/$package_name
archive=$dist_dir/$package_name.tar.gz
checksum=$archive.sha256

mkdir -p "$dist_dir"
rm -rf -- "$stage_dir"
rm -f -- "$archive" "$checksum"
mkdir -p "$stage_dir"
install -m 0644 COPYING "$stage_dir/COPYING"
sed "s/@VERSION@/$version/g" "$readme_template" > "$stage_dir/README.txt"
install -m 0644 contrib/release/THIRD_PARTY_NOTICES.txt \
	"$stage_dir/THIRD_PARTY_NOTICES.txt"
install -m 0644 contrib/config-example "$stage_dir/config-example"
install -m 0755 signalbox "$stage_dir/signalbox"

source_date_epoch=${SOURCE_DATE_EPOCH:-}
if [ -z "$source_date_epoch" ]; then
	source_date_epoch=$(git log -1 --format=%ct 2>/dev/null || printf '0')
fi
case "$source_date_epoch" in
	''|*[!0-9]*)
		echo "error: SOURCE_DATE_EPOCH must be an integer" >&2
		exit 1
		;;
esac

python3 scripts/create-release-archive.py \
	"$stage_dir" "$archive" "$source_date_epoch"

archive_name=$(basename "$archive")
if [ "$package_platform" = macos ]; then
	(cd "$dist_dir" && shasum -a 256 "$archive_name") > "$checksum"
else
	(cd "$dist_dir" && sha256sum "$archive_name") > "$checksum"
fi

echo "Created $archive"
echo "Created $checksum"
