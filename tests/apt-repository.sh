#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
version=$(<"$project_root/VERSION")
readonly version

for command_name in apt-ftparchive dpkg-scanpackages dpkg-scansources gpg gpgv; do
    command -v "$command_name" >/dev/null 2>&1 || {
        printf 'SKIP signed APT repository（缺少 %s）\n' "$command_name"
        exit 0
    }
done

tmp_dir=$(mktemp -d)
trap 'rm -rf -- "$tmp_dir"' EXIT
gnupg_home="$tmp_dir/gnupg"
repo="$tmp_dir/repository"
install -d -m0700 "$gnupg_home"
test_passphrase='isolated-apt-signing-passphrase'
test_passphrase_file="$tmp_dir/test-passphrase"
printf '%s\n' "$test_passphrase" >"$test_passphrase_file"
chmod 0600 "$test_passphrase_file"
gpg --batch --homedir "$gnupg_home" --pinentry-mode loopback \
    --passphrase "$test_passphrase" \
    --quick-generate-key \
    'UU Remote APT Test <apt-test@invalid.example>' ed25519 sign 1d \
    >/dev/null 2>&1
fingerprint=$(gpg --batch --homedir "$gnupg_home" --with-colons \
    --list-secret-keys | awk -F: '$1 == "fpr" {print $10; exit}')
[[ $fingerprint =~ ^[A-F0-9]{40,64}$ ]]
test_public_key="$tmp_dir/test-public-key.gpg"
gpg --batch --homedir "$gnupg_home" --output "$test_public_key" \
    --export "$fingerprint"

SOURCE_DATE_EPOCH=1787414400 \
    "$project_root/packaging/apt/build-repository.sh" \
    --output "$repo" \
    --gnupg-home "$gnupg_home" \
    --signing-key "$fingerprint" \
    --passphrase-file "$test_passphrase_file" \
    --expected-public-key "$test_public_key" >/dev/null

if "$project_root/packaging/apt/build-repository.sh" \
    --output "$tmp_dir/mismatched-key-repository" \
    --gnupg-home "$gnupg_home" \
    --signing-key "$fingerprint" \
    --passphrase-file "$test_passphrase_file" \
    --expected-public-key \
        "$project_root/packaging/apt/uu-remote-for-linux-archive-keyring.gpg" \
    >/dev/null 2>&1; then
    printf '仓库生成器接受了与固定公钥不匹配的私钥。\n' >&2
    exit 1
fi

chmod 0644 "$test_passphrase_file"
if "$project_root/packaging/apt/build-repository.sh" \
    --output "$tmp_dir/weak-passphrase-permissions" \
    --gnupg-home "$gnupg_home" \
    --signing-key "$fingerprint" \
    --passphrase-file "$test_passphrase_file" >/dev/null 2>&1; then
    printf '仓库生成器接受了权限过宽的签名口令文件。\n' >&2
    exit 1
fi
chmod 0600 "$test_passphrase_file"

(
    cd "$repo"
    sha256sum --strict -c SHA256SUMS >/dev/null
)
gpgv --keyring "$repo/uu-remote-for-linux-archive-keyring.gpg" \
    "$repo/dists/noble/InRelease" >/dev/null 2>&1
gpgv --keyring "$repo/uu-remote-for-linux-archive-keyring.gpg" \
    "$repo/dists/noble/Release.gpg" "$repo/dists/noble/Release" \
    >/dev/null 2>&1

packages="$repo/dists/noble/main/binary-amd64/Packages"
sources="$repo/dists/noble/main/source/Sources"
[[ $(grep -c '^Package: ' "$packages") -eq 8 ]]
for package in \
    uu-remote-for-linux \
    uu-remote-for-linux-x11 \
    uu-remote-for-linux-wayland \
    uu-remote-for-linux-archive-keyring \
    gir1.2-mutter-14 \
    libmutter-14-0 \
    mutter-common \
    mutter-common-bin; do
    [[ $(grep -c "^Package: $package$" "$packages") -eq 1 ]]
done
grep -A20 '^Package: uu-remote-for-linux-x11$' "$packages" | \
    grep -Fqx "Depends: uu-remote-for-linux (= $version)"
wayland_stanza=$(grep -A25 '^Package: uu-remote-for-linux-wayland$' "$packages")
grep -Fq "uu-remote-for-linux (= $version)" <<<"$wayland_stanza"
grep -Fq 'base-files (>= 13ubuntu10)' <<<"$wayland_stanza"
grep -Fq 'base-files (<< 13ubuntu11~)' <<<"$wayland_stanza"
grep -Fq 'gnome-shell (>= 46~)' <<<"$wayland_stanza"
grep -Fq 'gnome-shell (<< 47~)' <<<"$wayland_stanza"
grep -Fq 'libmutter-14-0 (>= 46.2-1ubuntu0.24.04.16+0uuremote3)' \
    <<<"$wayland_stanza"
if grep -Fq 'libmutter-14-0 (= 46.2-1ubuntu0.24.04.16+0uuremote3)' \
    <<<"$wayland_stanza"; then
    printf 'Wayland 入口错误地阻止了未来 Ubuntu Mutter 更新。\n' >&2
    exit 1
fi
dpkg --compare-versions \
    '46.2-1ubuntu0.24.04.16+0uuremote3' lt \
    '46.2-1ubuntu0.24.04.17'
[[ $(grep -c '^Provides: uu-remote-for-linux-session$' "$packages") -eq 2 ]]
[[ $(grep -c '^Conflicts: uu-remote-for-linux-session$' "$packages") -eq 2 ]]
grep -Fqx 'Package: mutter' "$sources"
grep -Fqx 'Version: 46.2-1ubuntu0.24.04.16+0uuremote3' "$sources"

for flavor in x11 wayland; do
    stage="$tmp_dir/$flavor-control"
    dpkg-deb -e \
        "$project_root/dist/uu-remote-for-linux-${flavor}_${version}_amd64.deb" \
        "$stage"
    [[ $(find "$stage" -mindepth 1 -maxdepth 1 -type f -printf '%f\n') == control ]]
done

keyring_stage="$tmp_dir/keyring"
dpkg-deb -x \
    "$project_root/dist/uu-remote-for-linux-archive-keyring_${version}_all.deb" \
    "$keyring_stage"
cmp \
    "$repo/uu-remote-for-linux-archive-keyring.gpg" \
    "$keyring_stage/usr/share/keyrings/uu-remote-for-linux-archive-keyring.gpg"
grep -Fqx 'Signed-By: /usr/share/keyrings/uu-remote-for-linux-archive-keyring.gpg' \
    "$keyring_stage/etc/apt/sources.list.d/uu-remote-for-linux.sources"

if rg -q 'APT_SIGNING_PRIVATE_KEY|APT_SIGNING_PASSPHRASE' \
    "$project_root/.github/workflows"; then
    printf 'GitHub workflow 不得读取 APT 生产签名秘密。\n' >&2
    exit 1
fi

printf '签名 APT 仓库隔离测试通过。\n'
