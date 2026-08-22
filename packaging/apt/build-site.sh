#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd -P)
readonly project_root

usage() {
    printf '用法: %s --output EMPTY_DIR --gnupg-home DIR --signing-key FINGERPRINT --passphrase-file FILE\n' "$0" >&2
    exit 64
}

output=
gnupg_home=
signing_key=
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
        --passphrase-file)
            (($# >= 2)) || usage
            passphrase_file=$2
            shift 2
            ;;
        *) usage ;;
    esac
done
[[ -n $output && -n $gnupg_home && -n $signing_key && \
   -n $passphrase_file ]] || usage
if [[ -e $output ]] && find "$output" -mindepth 1 -print -quit | grep -q .; then
    printf '站点输出目录必须不存在或为空: %s\n' "$output" >&2
    exit 73
fi

mkdir -p "$output"
install -m0644 "$project_root/packaging/apt/index.html" "$output/index.html"
install -m0644 /dev/null "$output/.nojekyll"
"$project_root/packaging/apt/build-repository.sh" \
    --output "$output/apt" \
    --gnupg-home "$gnupg_home" \
    --signing-key "$signing_key" \
    --passphrase-file "$passphrase_file" \
    --expected-public-key \
        "$project_root/packaging/apt/uu-remote-for-linux-archive-keyring.gpg"
printf 'Static signed Pages site: %s\n' "$output"
