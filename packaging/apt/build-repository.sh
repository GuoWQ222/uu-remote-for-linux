#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)
readonly project_root
version=$(<"$project_root/VERSION")
readonly version
readonly mutter_binary_dir="$project_root/third_party/mutter/ubuntu-24.04-amd64/uuremote3/packages"
readonly mutter_source_dir="$project_root/third_party/mutter/source/46.2-1ubuntu0.24.04.16-uuremote3"

usage() {
    printf '用法: %s --output DIR --gnupg-home DIR --signing-key FINGERPRINT [--passphrase-file FILE] [--expected-public-key FILE]\n' "$0" >&2
    exit 64
}

output=
gnupg_home=
signing_key=
expected_public_key=
passphrase_file=
while (($#)); do
    case $1 in
        --output)
            (($# >= 2)) || usage
            output=$2
            shift 2
            ;;
        --gnupg-home)
            (($# >= 2)) || usage
            gnupg_home=$2
            shift 2
            ;;
        --signing-key)
            (($# >= 2)) || usage
            signing_key=$2
            shift 2
            ;;
        --expected-public-key)
            (($# >= 2)) || usage
            expected_public_key=$2
            shift 2
            ;;
        --passphrase-file)
            (($# >= 2)) || usage
            passphrase_file=$2
            shift 2
            ;;
        *) usage ;;
    esac
done
[[ -n $output && -n $gnupg_home && -n $signing_key ]] || usage
[[ $signing_key =~ ^[A-Fa-f0-9]{40,64}$ ]] || {
    printf '签名密钥必须使用完整 OpenPGP 指纹。\n' >&2
    exit 65
}
[[ -d $gnupg_home && ! -L $gnupg_home ]] || {
    printf 'GNUPGHOME 不存在或不可信。\n' >&2
    exit 65
}
if [[ -n $passphrase_file ]]; then
    [[ -f $passphrase_file && ! -L $passphrase_file ]] || {
        printf '签名口令文件不存在或是符号链接。\n' >&2
        exit 65
    }
    passphrase_owner=$(stat -c %u "$passphrase_file")
    passphrase_mode=$(stat -c %a "$passphrase_file")
    [[ $passphrase_owner == "$(id -u)" && $passphrase_mode == 600 ]] || {
        printf '签名口令文件必须由当前用户拥有且权限严格为 0600。\n' >&2
        exit 65
    }
fi
if [[ -e $output ]] && find "$output" -mindepth 1 -print -quit | grep -q .; then
    printf '输出目录必须不存在或为空: %s\n' "$output" >&2
    exit 73
fi
for command_name in apt-ftparchive dpkg-scanpackages dpkg-scansources dpkg-deb \
    gpg gpgv gzip; do
    command -v "$command_name" >/dev/null 2>&1 || {
        printf '缺少仓库构建工具: %s\n' "$command_name" >&2
        exit 69
    }
done
gpg --batch --homedir "$gnupg_home" --list-secret-keys "$signing_key" \
    >/dev/null 2>&1 || {
    printf 'GNUPGHOME 中不存在指定签名私钥。\n' >&2
    exit 65
}

public_key=$(mktemp)
trap 'rm -f -- "$public_key"' EXIT
gpg --batch --homedir "$gnupg_home" --export "$signing_key" >"$public_key"
[[ -s $public_key ]] || {
    printf '无法导出 archive 公钥。\n' >&2
    exit 65
}
if [[ -n $expected_public_key ]]; then
    [[ -f $expected_public_key && ! -L $expected_public_key ]] || {
        printf '指定的预期公钥不存在或是符号链接。\n' >&2
        exit 65
    }
    cmp "$expected_public_key" "$public_key" >/dev/null || {
        printf '签名私钥与仓库内固定公钥不匹配。\n' >&2
        exit 65
    }
fi

"$project_root/packaging/build-deb.sh" >/dev/null
"$project_root/packaging/apt/build-packages.sh" \
    --archive-keyring "$public_key" >/dev/null

readonly binary_pool="pool/main/u/uu-remote-for-linux"
readonly mutter_pool="pool/main/m/mutter"
mkdir -p \
    "$output/$binary_pool" \
    "$output/$mutter_pool" \
    "$output/dists/noble/main/binary-amd64" \
    "$output/dists/noble/main/source"
touch "$output/.nojekyll"

install -m0644 \
    "$project_root/dist/uu-remote-for-linux_${version}_amd64.deb" \
    "$project_root/dist/uu-remote-for-linux-x11_${version}_amd64.deb" \
    "$project_root/dist/uu-remote-for-linux-wayland_${version}_amd64.deb" \
    "$project_root/dist/uu-remote-for-linux-archive-keyring_${version}_all.deb" \
    "$output/$binary_pool/"
find "$mutter_binary_dir" -maxdepth 1 -type f -name '*.deb' -print0 | \
    sort -z | xargs -0 -r install -m0644 -t "$output/$mutter_pool"
install -m0644 \
    "$mutter_source_dir/mutter_46.2.orig.tar.xz" \
    "$mutter_source_dir/mutter_46.2-1ubuntu0.24.04.16+0uuremote3.dsc" \
    "$mutter_source_dir/mutter_46.2-1ubuntu0.24.04.16+0uuremote3.debian.tar.xz" \
    "$output/$mutter_pool/"
install -m0644 "$public_key" \
    "$output/uu-remote-for-linux-archive-keyring.gpg"

(
    cd "$output"
    dpkg-scanpackages --multiversion pool /dev/null \
        >dists/noble/main/binary-amd64/Packages
    gzip -n -9 -c dists/noble/main/binary-amd64/Packages \
        >dists/noble/main/binary-amd64/Packages.gz
    dpkg-scansources pool /dev/null >dists/noble/main/source/Sources
    gzip -n -9 -c dists/noble/main/source/Sources \
        >dists/noble/main/source/Sources.gz
)

release_file="$output/dists/noble/Release"
release_date=$(date -Ru -d "@${SOURCE_DATE_EPOCH:-$(date +%s)}")
apt-ftparchive \
    -o "APT::FTPArchive::Release::Origin=UU Remote for Linux" \
    -o "APT::FTPArchive::Release::Label=UU Remote for Linux" \
    -o "APT::FTPArchive::Release::Suite=noble" \
    -o "APT::FTPArchive::Release::Codename=noble" \
    -o "APT::FTPArchive::Release::Architectures=amd64" \
    -o "APT::FTPArchive::Release::Components=main" \
    -o "APT::FTPArchive::Release::Description=Signed UU Remote for Linux packages" \
    -o "APT::FTPArchive::Release::Date=$release_date" \
    release "$output/dists/noble" >"$release_file"

sign_release() {
    if [[ -n $passphrase_file ]]; then
        gpg --batch --yes --no-tty --pinentry-mode loopback \
            --passphrase-file "$passphrase_file" --homedir "$gnupg_home" \
            --local-user "$signing_key" --digest-algo SHA256 "$@"
    else
        gpg --batch --yes --no-tty --homedir "$gnupg_home" \
            --local-user "$signing_key" --digest-algo SHA256 "$@"
    fi
}

sign_release --clearsign \
    --output "$output/dists/noble/InRelease" "$release_file"
sign_release --detach-sign \
    --output "$output/dists/noble/Release.gpg" "$release_file"

gpgv --keyring "$public_key" "$output/dists/noble/InRelease" >/dev/null
gpgv --keyring "$public_key" "$output/dists/noble/Release.gpg" \
    "$release_file" >/dev/null
checksum_tmp="$output/.SHA256SUMS.tmp"
(
    cd "$output"
    find . -type f ! -name SHA256SUMS ! -name .SHA256SUMS.tmp -print0 | \
        sort -z | xargs -0 sha256sum
) >"$checksum_tmp"
mv "$checksum_tmp" "$output/SHA256SUMS"
printf 'APT repository: %s\n' "$output"
printf 'Signing fingerprint: %s\n' "$signing_key"
