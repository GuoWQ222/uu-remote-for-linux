#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)
readonly project_root
readonly profile_dir="$project_root/third_party/mutter/ubuntu-24.04-amd64/uuremote3"
readonly source_dir="$project_root/third_party/mutter/source/46.2-1ubuntu0.24.04.16-uuremote3"
readonly base_version="46.2-1ubuntu0.24.04.16"
readonly target_version="46.2-1ubuntu0.24.04.16+0uuremote3"
readonly expected_maintainer="Wenqi Guo <129141646+GuoWQ222@users.noreply.github.com>"
readonly expected_legacy_library_sha256="79226513ee31551519a326b35793e2acc9a48a39929c5bca3906c3774f465e79"
readonly packages=(mutter-common mutter-common-bin libmutter-14-0 gir1.2-mutter-14)
readonly arches=(all amd64 amd64 amd64)

die() {
    printf 'Mutter bundle verification: %s\n' "$*" >&2
    exit 1
}

profile_value() {
    local wanted=$1 key value found=""
    while IFS='=' read -r key value; do
        case $key in ''|'#'*) continue ;; esac
        [[ $key =~ ^[a-z_][a-z0-9_]*$ &&
            $value =~ ^[A-Za-z0-9.+:_/-]+$ ]] || return 1
        if [[ $key == "$wanted" ]]; then
            [[ -z $found ]] || return 1
            found=$value
        fi
    done <"$profile_dir/profile.conf"
    [[ -n $found ]] || return 1
    printf '%s\n' "$found"
}

[[ -d $profile_dir && -d $source_dir ]] || die 'payload/source directory missing'
[[ -z $(find "$profile_dir" "$source_dir" -type l -print -quit) ]] ||
    die 'symbolic link is not allowed'

[[ $(profile_value format) == 1 ]] || die 'unsupported profile format'
[[ $(profile_value profile_id) == ubuntu-24.04-amd64-mutter-46.2-uuremote3 ]] ||
    die 'unexpected profile id'
[[ $(profile_value base_version) == "$base_version" ]] ||
    die 'base version mismatch'
[[ $(profile_value target_version) == "$target_version" ]] ||
    die 'target version mismatch'
profile_base_library_sha256=$(profile_value base_library_sha256) ||
    die 'missing base library hash'
profile_target_library_sha256=$(profile_value target_library_sha256) ||
    die 'missing target library hash'
profile_legacy_library_sha256=$(profile_value legacy_library_sha256) ||
    die 'missing legacy library hash'
[[ $profile_base_library_sha256 =~ ^[0-9a-f]{64}$ ]] ||
    die 'invalid base library hash'
[[ $profile_target_library_sha256 =~ ^[0-9a-f]{64}$ ]] ||
    die 'invalid target library hash'
[[ $profile_legacy_library_sha256 == "$expected_legacy_library_sha256" ]] ||
    die 'invalid legacy library hash'

expected_profile_files=$(mktemp)
actual_profile_files=$(mktemp)
expected_manifest_files=$(mktemp)
actual_manifest_files=$(mktemp)
expected_source_files=$(mktemp)
actual_source_files=$(mktemp)
expected_source_manifest_files=$(mktemp)
actual_source_manifest_files=$(mktemp)
extract_dir=$(mktemp -d)
target_extract_dir=$(mktemp -d)
base_extract_dir=$(mktemp -d)
trap 'rm -f -- "$expected_profile_files" "$actual_profile_files" "$expected_manifest_files" "$actual_manifest_files" "$expected_source_files" "$actual_source_files" "$expected_source_manifest_files" "$actual_source_manifest_files"; rm -rf -- "$extract_dir" "$target_extract_dir" "$base_extract_dir"' EXIT
{
    printf '%s\n' profile.conf SHA256SUMS
    for index in "${!packages[@]}"; do
        printf 'packages/%s_%s_%s.deb\n' \
            "${packages[index]}" "$target_version" "${arches[index]}"
        printf 'rollback/%s_%s_%s.deb\n' \
            "${packages[index]}" "$base_version" "${arches[index]}"
    done
} | sort >"$expected_profile_files"
find "$profile_dir" -type f -printf '%P\n' | sort >"$actual_profile_files"
cmp -s "$expected_profile_files" "$actual_profile_files" || {
    diff -u "$expected_profile_files" "$actual_profile_files" >&2 || true
    die 'payload file set differs from the allowlist'
}
grep -vxF SHA256SUMS "$expected_profile_files" >"$expected_manifest_files"
while IFS= read -r line; do
    [[ $line =~ ^[0-9a-f]{64}[[:space:]][[:space:]](.+)$ ]] ||
        die 'malformed payload checksum manifest line'
    printf '%s\n' "${BASH_REMATCH[1]}"
done <"$profile_dir/SHA256SUMS" | sort >"$actual_manifest_files"
[[ $(wc -l <"$actual_manifest_files") == 9 ]] ||
    die 'payload checksum manifest must contain exactly 9 entries'
[[ $(sort -u "$actual_manifest_files" | wc -l) == 9 ]] ||
    die 'payload checksum manifest contains duplicate entries'
cmp -s "$expected_manifest_files" "$actual_manifest_files" || {
    diff -u "$expected_manifest_files" "$actual_manifest_files" >&2 || true
    die 'payload checksum manifest does not cover the exact allowlist'
}
(cd "$profile_dir" && sha256sum --strict -c SHA256SUMS >/dev/null) ||
    die 'payload checksum failure'

for index in "${!packages[@]}"; do
    package=${packages[index]}
    arch=${arches[index]}
    patched="$profile_dir/packages/${package}_${target_version}_${arch}.deb"
    rollback="$profile_dir/rollback/${package}_${base_version}_${arch}.deb"
    for role in patched rollback; do
        if [[ $role == patched ]]; then
            file=$patched
            expected_version=$target_version
            expected_package_maintainer=$expected_maintainer
        else
            file=$rollback
            expected_version=$base_version
            expected_package_maintainer='Ubuntu Developers <ubuntu-devel-discuss@lists.ubuntu.com>'
        fi
        [[ $(dpkg-deb -f "$file" Package) == "$package" ]] ||
            die "$role package name mismatch: $package"
        [[ $(dpkg-deb -f "$file" Version) == "$expected_version" ]] ||
            die "$role version mismatch: $package"
        [[ $(dpkg-deb -f "$file" Architecture) == "$arch" ]] ||
            die "$role architecture mismatch: $package"
        [[ $(dpkg-deb -f "$file" Maintainer) == "$expected_package_maintainer" ]] ||
            die "$role maintainer mismatch: $package"
        source_name=$(dpkg-deb -f "$file" Source 2>/dev/null || true)
        [[ $source_name == mutter ]] ||
            die "$role source mismatch: $package"
        depends=$(dpkg-deb -f "$file" Depends 2>/dev/null || true)
        case $package in
            gir1.2-mutter-14)
                [[ $depends == *"libmutter-14-0 (= $expected_version)"* ]] ||
                    die "$role GIR dependency mismatch"
                ;;
            libmutter-14-0)
                [[ $depends == *"mutter-common-bin (= $expected_version)"* &&
                    $depends == *"mutter-common (>= $expected_version)"* ]] ||
                    die "$role library dependency mismatch"
                ;;
        esac
    done
done

target_library_deb="$profile_dir/packages/libmutter-14-0_${target_version}_amd64.deb"
base_library_deb="$profile_dir/rollback/libmutter-14-0_${base_version}_amd64.deb"
dpkg-deb -x "$target_library_deb" "$target_extract_dir"
dpkg-deb -x "$base_library_deb" "$base_extract_dir"
target_library_file="$target_extract_dir/usr/lib/x86_64-linux-gnu/libmutter-14.so.0.0.0"
base_library_file="$base_extract_dir/usr/lib/x86_64-linux-gnu/libmutter-14.so.0.0.0"
[[ -f $target_library_file && -f $base_library_file ]] ||
    die 'libmutter payload is missing the expected shared library'
target_library_sha256=$(sha256sum "$target_library_file")
target_library_sha256=${target_library_sha256%% *}
base_library_sha256=$(sha256sum "$base_library_file")
base_library_sha256=${base_library_sha256%% *}
[[ $target_library_sha256 == "$profile_target_library_sha256" ]] ||
    die 'target library hash does not match profile.conf'
[[ $base_library_sha256 == "$profile_base_library_sha256" ]] ||
    die 'base library hash does not match profile.conf'

dpkg --compare-versions "$base_version" lt "$target_version" ||
    die 'target does not sort newer than the supported base'
for future in \
    "$base_version+build1" "$base_version+esm1" \
    "$base_version+ubuntu1" "$base_version.1" \
    '46.2-1ubuntu0.24.04.17'; do
    dpkg --compare-versions "$target_version" lt "$future" ||
        die "target would shadow future version $future"
done

source_dsc="$source_dir/mutter_${target_version}.dsc"
{
    printf '%s\n' \
        BUILD-REPORT.md \
        SHA256SUMS \
        "mutter_${target_version}.debian.tar.xz" \
        "mutter_${target_version}.dsc" \
        "mutter_${target_version}_amd64.buildinfo" \
        "mutter_${target_version}_source.changes" \
        mutter_46.2.orig.tar.xz
} | sort >"$expected_source_files"
find "$source_dir" -maxdepth 1 -type f -printf '%P\n' |
    sort >"$actual_source_files"
cmp -s "$expected_source_files" "$actual_source_files" || {
    diff -u "$expected_source_files" "$actual_source_files" >&2 || true
    die 'corresponding source file set differs from the allowlist'
}
grep -vxF SHA256SUMS "$expected_source_files" >"$expected_source_manifest_files"
while IFS= read -r line; do
    [[ $line =~ ^[0-9a-f]{64}[[:space:]][[:space:]](.+)$ ]] ||
        die 'malformed source checksum manifest line'
    printf '%s\n' "${BASH_REMATCH[1]}"
done <"$source_dir/SHA256SUMS" | sort >"$actual_source_manifest_files"
[[ $(wc -l <"$actual_source_manifest_files") == 6 ]] ||
    die 'source checksum manifest must contain exactly 6 entries'
[[ $(sort -u "$actual_source_manifest_files" | wc -l) == 6 ]] ||
    die 'source checksum manifest contains duplicate entries'
cmp -s "$expected_source_manifest_files" "$actual_source_manifest_files" || {
    diff -u "$expected_source_manifest_files" "$actual_source_manifest_files" >&2 || true
    die 'source checksum manifest does not cover the exact allowlist'
}
(cd "$source_dir" && sha256sum --strict -c SHA256SUMS >/dev/null) ||
    die 'source checksum failure'
buildinfo="$source_dir/mutter_${target_version}_amd64.buildinfo"
grep -Fxq 'Source: mutter' "$buildinfo" ||
    die 'buildinfo source mismatch'
grep -Fxq "Version: $target_version" "$buildinfo" ||
    die 'buildinfo version mismatch'
grep -Fxq 'Build-Architecture: amd64' "$buildinfo" ||
    die 'buildinfo architecture mismatch'
for index in "${!packages[@]}"; do
    package=${packages[index]}
    arch=${arches[index]}
    file="$profile_dir/packages/${package}_${target_version}_${arch}.deb"
    file_name=${file##*/}
    file_size=$(stat -c %s "$file")
    file_sha256=$(sha256sum "$file")
    file_sha256=${file_sha256%% *}
    awk -v hash="$file_sha256" -v size="$file_size" -v name="$file_name" '
        $0 == "Checksums-Sha256:" { in_sha256 = 1; next }
        /^[A-Za-z][A-Za-z-]*:/ { in_sha256 = 0 }
        in_sha256 && $1 == hash && $2 == size && $3 == name { found = 1 }
        END { exit !found }
    ' "$buildinfo" || die "buildinfo does not bind runtime package: $package"
done
dpkg-source -x "$source_dsc" "$extract_dir/source" >/dev/null 2>&1 ||
    die 'corresponding source cannot be extracted'
source_version=$(dpkg-parsechangelog \
    -l"$extract_dir/source/debian/changelog" -SVersion)
[[ $source_version == "$target_version" ]] ||
    die 'source changelog version mismatch'
for patch in \
    uuremote-bound-monitor-memfd-capture-latency.patch \
    uuremote-retry-pipewire-overrun.patch; do
    grep -Fxq "$patch" "$extract_dir/source/debian/patches/series" ||
        die "source patch series omits $patch"
    cmp -s "$project_root/packaging/mutter/$patch" \
        "$extract_dir/source/debian/patches/$patch" ||
        die "source patch differs from repository copy: $patch"
done

printf 'PASS Mutter bundle %s\n' "$target_version"
