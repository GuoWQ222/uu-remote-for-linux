#!/usr/bin/env bash
set -euo pipefail

fixture_version_for_package() {
    local wanted=$1 package version

    while IFS=$'\t' read -r package version; do
        if [[ $package == "$wanted" ]]; then
            printf '%s\n' "$version"
            return 0
        fi
    done <"${UU_REMOTE_MUTTER_FIXTURE_VERSIONS:?}"
    return 1
}

fixture_dpkg_query() {
    local argument selected="" version arch status status_mode relaxed=0

    for argument in "$@"; do
        # shellcheck disable=SC2016 # Match dpkg-query's literal format tokens.
        if [[ $argument == *'${Version}'* && $argument != *'${Status}'* ]]; then
            relaxed=1
        fi
        case $argument in
            gnome-shell|mutter-common|mutter-common-bin|libmutter-14-0|gir1.2-mutter-14)
                selected=$argument
                ;;
        esac
    done
    if [[ $selected == gnome-shell ]]; then
        [[ ${UU_REMOTE_MUTTER_FIXTURE_GNOME_INSTALLED:-1} == 1 ]] || return 1
        printf 'install ok installed\n'
        return 0
    fi
    if [[ -n $selected ]]; then
        version=$(fixture_version_for_package "$selected") || return 1
        if [[ $selected == mutter-common ]]; then
            arch=all
        else
            arch=amd64
        fi
        if ((relaxed)); then
            printf '%s\t%s\n' "$version" "$arch"
        else
            status_mode=$(/usr/bin/cat \
                "${UU_REMOTE_MUTTER_FIXTURE_PACKAGE_STATUS_FILE:?}") ||
                return 1
            case $status_mode in
                installed)
                    status='install ok installed'
                    ;;
                unpacked)
                    status='install ok unpacked'
                    ;;
                interrupted)
                    status='install ok installed'
                    if [[ $selected == mutter-common-bin ]]; then
                        status='install ok unpacked'
                    fi
                    ;;
                *) return 64 ;;
            esac
            printf '%s\t%s\t%s\n' "$status" "$version" "$arch"
        fi
        return 0
    fi
    if [[ ${UU_REMOTE_MUTTER_FIXTURE_DPKG_QUERY_GLOBAL:-ok} == fail ]]; then
        return 71
    fi
    if [[ ${UU_REMOTE_MUTTER_FIXTURE_FOREIGN_ARCH:-0} == 1 ]]; then
        printf 'libmutter-14-0:i386\tii \n'
    fi
}

fixture_dpkg() {
    case ${1:-} in
        --print-architecture)
            printf '%s\n' "${UU_REMOTE_MUTTER_FIXTURE_ARCH:-amd64}"
            ;;
        --audit)
            case ${UU_REMOTE_MUTTER_FIXTURE_AUDIT:-clean} in
                clean) ;;
                dirty) printf 'mock incomplete dpkg transaction\n' ;;
                fail) return 72 ;;
                *) return 64 ;;
            esac
            ;;
        --compare-versions)
            /usr/bin/dpkg "$@"
            ;;
        *)
            printf 'unexpected fake dpkg invocation: %q' "$1" >&2
            printf ' %q' "${@:2}" >&2
            printf '\n' >&2
            return 64
            ;;
    esac
}

fixture_dpkg_deb() {
    local file field stem package version arch

    [[ ${1:-} == -f && $# == 3 ]] || return 64
    file=$2
    field=$3
    stem=${file##*/}
    stem=${stem%.deb}
    package=${stem%%_*}
    version=${stem#*_}
    version=${version%_*}
    arch=${stem##*_}
    case $field in
        Package) printf '%s\n' "$package" ;;
        Version) printf '%s\n' "$version" ;;
        Architecture) printf '%s\n' "$arch" ;;
        Depends)
            case $package in
                gir1.2-mutter-14)
                    printf 'libmutter-14-0 (= %s)\n' "$version"
                    ;;
                libmutter-14-0)
                    printf 'mutter-common-bin (= %s), mutter-common (>= %s)\n' \
                        "$version" "$version"
                    ;;
                *) printf '\n' ;;
            esac
            ;;
        *) return 64 ;;
    esac
}

fixture_apt_mark() {
    local package

    printf 'apt-mark' >>"${UU_REMOTE_MUTTER_FIXTURE_TRACE:?}"
    printf ' %q' "$@" >>"$UU_REMOTE_MUTTER_FIXTURE_TRACE"
    printf '\n' >>"$UU_REMOTE_MUTTER_FIXTURE_TRACE"
    case ${1:-} in
        showhold)
            [[ ${UU_REMOTE_MUTTER_FIXTURE_SHOWHOLD:-ok} != fail ]] || return 73
            [[ -r ${UU_REMOTE_MUTTER_FIXTURE_HOLDS:?} ]] &&
                /usr/bin/cat "$UU_REMOTE_MUTTER_FIXTURE_HOLDS"
            ;;
        showauto)
            [[ ${UU_REMOTE_MUTTER_FIXTURE_SHOWAUTO:-ok} != fail ]] || return 74
            printf '%s\n' \
                mutter-common mutter-common-bin libmutter-14-0 gir1.2-mutter-14
            ;;
        auto|manual)
            for package in "${@:2}"; do
                case $package in
                    mutter-common|mutter-common-bin|libmutter-14-0|gir1.2-mutter-14) ;;
                    *) return 64 ;;
                esac
            done
            ;;
        *) return 64 ;;
    esac
}

fixture_write_versions() {
    local version=$1

    {
        printf 'mutter-common\t%s\n' "$version"
        printf 'mutter-common-bin\t%s\n' "$version"
        printf 'libmutter-14-0\t%s\n' "$version"
        printf 'gir1.2-mutter-14\t%s\n' "$version"
    } >"${UU_REMOTE_MUTTER_FIXTURE_VERSIONS:?}"
}

fixture_write_mixed_versions() {
    {
        printf 'mutter-common\t%s\n' \
            "${UU_REMOTE_MUTTER_FIXTURE_TARGET_VERSION:?}"
        printf 'mutter-common-bin\t%s\n' \
            "${UU_REMOTE_MUTTER_FIXTURE_TARGET_VERSION:?}"
        printf 'libmutter-14-0\t%s\n' \
            "${UU_REMOTE_MUTTER_FIXTURE_BASE_VERSION:?}"
        printf 'gir1.2-mutter-14\t%s\n' \
            "${UU_REMOTE_MUTTER_FIXTURE_BASE_VERSION:?}"
    } >"${UU_REMOTE_MUTTER_FIXTURE_VERSIONS:?}"
}

fixture_set_package_status() {
    printf '%s\n' "$1" >"${UU_REMOTE_MUTTER_FIXTURE_PACKAGE_STATUS_FILE:?}"
}

fixture_apt_simulation() {
    local transaction_kind=$1 package expected_version
    local -a packages=(
        mutter-common
        mutter-common-bin
        libmutter-14-0
        gir1.2-mutter-14
    )

    if [[ $transaction_kind == install || $transaction_kind == normalize ]]; then
        expected_version=${UU_REMOTE_MUTTER_FIXTURE_TARGET_VERSION:?}
    else
        expected_version=${UU_REMOTE_MUTTER_FIXTURE_BASE_VERSION:?}
    fi
    for package in "${packages[@]}"; do
        printf 'Inst %s [mock-old] (%s local [amd64])\n' \
            "$package" "$expected_version"
    done
    for package in "${packages[@]}"; do
        printf 'Conf %s (%s local [amd64])\n' \
            "$package" "$expected_version"
    done
    if [[ ${UU_REMOTE_MUTTER_FIXTURE_APT_SIMULATION:-valid} == unexpected-remove ]]; then
        printf 'Remv gnome-shell [46.0]\n'
    fi
    if [[ $transaction_kind == install ]]; then
        printf '4 upgraded, 0 newly installed, 0 to remove and 0 not upgraded.\n'
    else
        printf '0 upgraded, 0 newly installed, 4 downgraded, 0 to remove and 0 not upgraded.\n'
    fi
}

fixture_apt_get() {
    local argument simulation=0 allow_downgrades=0 target_payload=0
    local configure_pending_override=0
    local transaction_kind real_mode preinvoke_action="" preinstall_action=""
    local -a payload_files=() plan_files=()

    printf 'apt-get' >>"${UU_REMOTE_MUTTER_FIXTURE_TRACE:?}"
    printf ' %q' "$@" >>"$UU_REMOTE_MUTTER_FIXTURE_TRACE"
    printf '\n' >>"$UU_REMOTE_MUTTER_FIXTURE_TRACE"
    if [[ ${1:-} == -s && ${2:-} == check ]]; then
        [[ ${UU_REMOTE_MUTTER_FIXTURE_APT_HEALTH:-healthy} == healthy ]]
        return
    fi
    for argument in "$@"; do
        [[ $argument != -s ]] || simulation=1
        [[ $argument != --allow-downgrades ]] || allow_downgrades=1
        [[ $argument != *'DPkg::ConfigurePending=false'* ]] ||
            configure_pending_override=1
        case $argument in
            *.deb)
                payload_files+=("$argument")
                [[ $argument != */packages/*.deb ]] || target_payload=1
                ;;
            DPkg::Pre-Invoke::=*) preinvoke_action=${argument##* } ;;
            DPkg::Pre-Install-Pkgs::=*) preinstall_action=${argument##* } ;;
        esac
    done
    ((configure_pending_override == 0)) || return 80
    if ((target_payload)); then
        if ((allow_downgrades)); then
            transaction_kind=normalize
        else
            transaction_kind=install
        fi
    else
        transaction_kind=rollback
    fi
    if ((simulation)); then
        fixture_apt_simulation "$transaction_kind"
        return
    fi
    [[ $preinvoke_action == preinvoke-* &&
        $preinstall_action == preinstall-* ]] || return 77
    case ${UU_REMOTE_MUTTER_FIXTURE_PREINVOKE_MUTATION:-none} in
        none) ;;
        future)
            if [[ ! -e ${UU_REMOTE_MUTTER_FIXTURE_PREINVOKE_ONCE:?} ]]; then
                fixture_write_versions \
                    "${UU_REMOTE_MUTTER_FIXTURE_FUTURE_VERSION:?}"
                /usr/bin/cp -- "${UU_REMOTE_MUTTER_FIXTURE_TARGET_LIBRARY:?}" \
                    "${UU_REMOTE_MUTTER_FIXTURE_INSTALLED_LIBRARY:?}"
                : >"$UU_REMOTE_MUTTER_FIXTURE_PREINVOKE_ONCE"
            fi
            ;;
        audit-dirty)
            export UU_REMOTE_MUTTER_FIXTURE_AUDIT=dirty
            ;;
        *) return 64 ;;
    esac
    "${UU_REMOTE_MUTTER_FIXTURE_MANAGER:?}" "$preinvoke_action" || return
    plan_files=("${payload_files[@]}")
    case ${UU_REMOTE_MUTTER_FIXTURE_PREINSTALL_PLAN:-valid} in
        valid) ;;
        extra)
            plan_files+=("${UU_REMOTE_MUTTER_FIXTURE_EXTRA_DEB:?}")
            ;;
        wrong-version)
            ((${#plan_files[@]} >= 1)) || return 78
            plan_files[0]=${UU_REMOTE_MUTTER_FIXTURE_WRONG_DEB:?}
            ;;
        *) return 64 ;;
    esac
    printf '%s\n' "${plan_files[@]}" |
        "${UU_REMOTE_MUTTER_FIXTURE_MANAGER:?}" "$preinstall_action" || return
    real_mode=${UU_REMOTE_MUTTER_FIXTURE_APT_REAL_MODE:-success}
    if [[ $real_mode == target-no-write-fail && $target_payload == 1 ]]; then
        return 79
    fi
    fixture_set_package_status unpacked
    if [[ $real_mode == target-success-wrong && $target_payload == 1 ]]; then
        printf 'fixture-write target-wrong\n' \
            >>"$UU_REMOTE_MUTTER_FIXTURE_TRACE"
        fixture_write_versions "${UU_REMOTE_MUTTER_FIXTURE_TARGET_VERSION:?}"
        /usr/bin/cp -- "${UU_REMOTE_MUTTER_FIXTURE_BASE_LIBRARY:?}" \
            "${UU_REMOTE_MUTTER_FIXTURE_INSTALLED_LIBRARY:?}"
        fixture_set_package_status installed
        return 0
    fi
    if [[ $real_mode == target-partial-fail && $target_payload == 1 ]]; then
        printf 'fixture-write target-partial\n' \
            >>"$UU_REMOTE_MUTTER_FIXTURE_TRACE"
        fixture_write_mixed_versions
        /usr/bin/cp -- "${UU_REMOTE_MUTTER_FIXTURE_TARGET_LIBRARY:?}" \
            "${UU_REMOTE_MUTTER_FIXTURE_INSTALLED_LIBRARY:?}"
        return 75
    fi
    if [[ $real_mode == rollback-partial-fail-once && $target_payload == 0 &&
        ! -e ${UU_REMOTE_MUTTER_FIXTURE_APT_ONCE:?} ]]; then
        printf 'fixture-write rollback-partial\n' \
            >>"$UU_REMOTE_MUTTER_FIXTURE_TRACE"
        fixture_write_mixed_versions
        /usr/bin/cp -- "${UU_REMOTE_MUTTER_FIXTURE_TARGET_LIBRARY:?}" \
            "${UU_REMOTE_MUTTER_FIXTURE_INSTALLED_LIBRARY:?}"
        : >"$UU_REMOTE_MUTTER_FIXTURE_APT_ONCE"
        return 76
    fi
    if ((target_payload)); then
        printf 'fixture-write target\n' >>"$UU_REMOTE_MUTTER_FIXTURE_TRACE"
        fixture_write_versions "${UU_REMOTE_MUTTER_FIXTURE_TARGET_VERSION:?}"
        /usr/bin/cp -- "${UU_REMOTE_MUTTER_FIXTURE_TARGET_LIBRARY:?}" \
            "${UU_REMOTE_MUTTER_FIXTURE_INSTALLED_LIBRARY:?}"
    else
        printf 'fixture-write base\n' >>"$UU_REMOTE_MUTTER_FIXTURE_TRACE"
        fixture_write_versions "${UU_REMOTE_MUTTER_FIXTURE_BASE_VERSION:?}"
        /usr/bin/cp -- "${UU_REMOTE_MUTTER_FIXTURE_BASE_LIBRARY:?}" \
            "${UU_REMOTE_MUTTER_FIXTURE_INSTALLED_LIBRARY:?}"
    fi
    fixture_set_package_status installed
}

fixture_flock() {
    /usr/bin/flock "$@" || return
    case ${UU_REMOTE_MUTTER_FIXTURE_FLOCK_MUTATION:-none} in
        none) ;;
        state-to-target)
            fixture_write_versions "${UU_REMOTE_MUTTER_FIXTURE_TARGET_VERSION:?}"
            /usr/bin/cp -- "${UU_REMOTE_MUTTER_FIXTURE_TARGET_LIBRARY:?}" \
                "${UU_REMOTE_MUTTER_FIXTURE_INSTALLED_LIBRARY:?}"
            ;;
        corrupt-payload)
            printf 'mutated after flock\n' \
                >>"${UU_REMOTE_MUTTER_FIXTURE_MUTATION_FILE:?}"
            ;;
        *) return 64 ;;
    esac
}

fixture_pgrep() {
    case ${UU_REMOTE_MUTTER_FIXTURE_PGREP:-absent} in
        absent) return 1 ;;
        running)
            printf '%s\n' "${UU_REMOTE_MUTTER_FIXTURE_SHELL_PID:?}"
            ;;
        fail) return 70 ;;
        *) return 64 ;;
    esac
}

case ${0##*/} in
    dpkg-query) fixture_dpkg_query "$@"; exit ;;
    dpkg) fixture_dpkg "$@"; exit ;;
    dpkg-deb) fixture_dpkg_deb "$@"; exit ;;
    apt-get) fixture_apt_get "$@"; exit ;;
    apt-mark) fixture_apt_mark "$@"; exit ;;
    flock) fixture_flock "$@"; exit ;;
    pgrep) fixture_pgrep "$@"; exit ;;
esac

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly manager="$project_root/lib/uu-remote-for-linux/uu-remote-mutter-fix-root"
readonly frontend="$project_root/lib/uu-remote-for-linux/uu-remote-mutter-fix"
readonly base_version=46.2-1ubuntu0.24.04.16
readonly target_version=46.2-1ubuntu0.24.04.16+0uuremote3
readonly legacy_version=46.2-1ubuntu0.24.04.16+uuremote3
readonly future_version=46.2-1ubuntu0.24.04.17
readonly packages=(
    mutter-common
    mutter-common-bin
    libmutter-14-0
    gir1.2-mutter-14
)
readonly arches=(all amd64 amd64 amd64)

test_root=$(mktemp -d)
trap 'rm -rf -- "$test_root"' EXIT
readonly test_root
readonly sysroot="$test_root/root"
readonly fake_bin="$test_root/bin"
readonly profile="$test_root/profile"
readonly versions_file="$test_root/versions.tsv"
readonly package_status_file="$test_root/package-status"
readonly holds_file="$test_root/holds"
readonly trace_file="$test_root/commands.log"
readonly apt_once_file="$test_root/apt-once"
readonly preinvoke_once_file="$test_root/preinvoke-once"
readonly shell_pid=4242
readonly shell_library_path=/usr/lib/x86_64-linux-gnu/libmutter-14.so.0.0.0
readonly base_library="$test_root/base-libmutter.so"
readonly target_library="$test_root/target-libmutter.so"
readonly legacy_library="$test_root/legacy-libmutter.so"
readonly extra_deb="$test_root/unrelated-package_1.0_amd64.deb"
readonly wrong_deb="$test_root/mutter-common_${future_version}_all.deb"
readonly installed_library="$sysroot/usr/lib/x86_64-linux-gnu/libmutter-14.so.0.0.0"
readonly transaction_marker=\
"$sysroot/var/lib/uu-remote-for-linux/mutter-fix/transaction-started"

fail() {
    printf 'Mutter manager 测试失败：%s\n' "$*" >&2
    exit 1
}

write_os_release() {
    local id=${1:-ubuntu} version=${2:-24.04} codename=${3:-noble}

    {
        printf 'ID=%s\n' "$id"
        printf 'VERSION_ID="%s"\n' "$version"
        printf 'VERSION_CODENAME=%s\n' "$codename"
    } >"$sysroot/etc/os-release"
}

set_versions() {
    fixture_write_versions "$1"
}

set_stale_shell_mapping() {
    local disk_inode

    disk_inode=$(/usr/bin/stat -Lc '%i' -- "$installed_library")
    /usr/bin/mkdir -p -- "$sysroot/proc/$shell_pid"
    printf '7f000000-7f001000 r-xp 00000000 00:00 %s %s\n' \
        "$((disk_inode + 1))" "$shell_library_path" \
        >"$sysroot/proc/$shell_pid/maps"
    export UU_REMOTE_MUTTER_FIXTURE_PGREP=running
}

write_profile() {
    local index package arch file base_hash target_hash legacy_hash

    /usr/bin/mkdir -p -- "$profile/packages" "$profile/rollback"
    for index in "${!packages[@]}"; do
        package=${packages[index]}
        arch=${arches[index]}
        file="$profile/packages/${package}_${target_version}_${arch}.deb"
        printf 'mock target %s %s\n' "$package" "$arch" >"$file"
        file="$profile/rollback/${package}_${base_version}_${arch}.deb"
        printf 'mock rollback %s %s\n' "$package" "$arch" >"$file"
    done
    base_hash=$(/usr/bin/sha256sum "$base_library")
    base_hash=${base_hash%% *}
    target_hash=$(/usr/bin/sha256sum "$target_library")
    target_hash=${target_hash%% *}
    legacy_hash=$(/usr/bin/sha256sum "$legacy_library")
    legacy_hash=${legacy_hash%% *}
    {
        printf 'format=1\n'
        printf 'profile_id=ubuntu-24.04-amd64-mutter-46.2-uuremote3\n'
        printf 'base_version=%s\n' "$base_version"
        printf 'target_version=%s\n' "$target_version"
        printf 'base_library_sha256=%s\n' "$base_hash"
        printf 'target_library_sha256=%s\n' "$target_hash"
        printf 'legacy_library_sha256=%s\n' "$legacy_hash"
    } >"$profile/profile.conf"
    (
        cd -- "$profile"
        /usr/bin/sha256sum \
            profile.conf packages/*.deb rollback/*.deb >SHA256SUMS
    )
}

reset_fixture() {
    /usr/bin/rm -rf -- "${sysroot:?}/var" "$sysroot/run" "$sysroot/proc"
    write_os_release
    set_versions "$base_version"
    : >"$holds_file"
    : >"$trace_file"
    export UU_REMOTE_MUTTER_FIXTURE_ARCH=amd64
    export UU_REMOTE_MUTTER_FIXTURE_GNOME_INSTALLED=1
    export UU_REMOTE_MUTTER_FIXTURE_PGREP=absent
    export UU_REMOTE_MUTTER_FIXTURE_FOREIGN_ARCH=0
    export UU_REMOTE_MUTTER_FIXTURE_DPKG_QUERY_GLOBAL=ok
    fixture_set_package_status installed
    export UU_REMOTE_MUTTER_FIXTURE_AUDIT=clean
    export UU_REMOTE_MUTTER_FIXTURE_SHOWHOLD=ok
    export UU_REMOTE_MUTTER_FIXTURE_SHOWAUTO=ok
    export UU_REMOTE_MUTTER_FIXTURE_APT_HEALTH=healthy
    export UU_REMOTE_MUTTER_FIXTURE_APT_SIMULATION=valid
    export UU_REMOTE_MUTTER_FIXTURE_APT_REAL_MODE=success
    export UU_REMOTE_MUTTER_FIXTURE_FLOCK_MUTATION=none
    export UU_REMOTE_MUTTER_FIXTURE_PREINVOKE_MUTATION=none
    export UU_REMOTE_MUTTER_FIXTURE_PREINSTALL_PLAN=valid
    /usr/bin/rm -f -- "$apt_once_file" "$preinvoke_once_file"
    /usr/bin/cp -- "$base_library" "$installed_library"
    write_profile
}

run_status() {
    "$manager" status
}

expect_status() {
    local expected_state=$1 expected_reason=$2 output

    output=$(run_status) || fail "status 命令失败：$output"
    grep -Fxq "state=$expected_state" <<<"$output" ||
        fail "预期 state=$expected_state，实际输出：$output"
    grep -Fxq "reason=$expected_reason" <<<"$output" ||
        fail "预期 reason=$expected_reason，实际输出：$output"
}

expect_payload_corrupt() {
    local label=$1 status_code

    set +e
    run_status >"$test_root/payload-corrupt.out" \
        2>"$test_root/payload-corrupt.err"
    status_code=$?
    set -e
    [[ $status_code == 2 ]] ||
        fail "$label 应返回 2，实际为 $status_code。"
    grep -Fxq 'state=payload-corrupt' "$test_root/payload-corrupt.out" ||
        fail "$label 未被报告为 payload-corrupt。"
}

assert_no_real_apt() {
    local label=$1

    if grep -q '^apt-get -y ' "$trace_file"; then
        fail "$label 后仍执行了真实 APT 事务。"
    fi
}

assert_all_packages_configured() {
    local package record status version arch

    [[ $(/usr/bin/cat "$package_status_file") == installed ]] ||
        fail 'APT 成功事务结束后 fixture 仍处于未配置状态。'
    for package in "${packages[@]}"; do
        # shellcheck disable=SC2016 # dpkg-query expands this format string.
        record=$("$fake_bin/dpkg-query" -W \
            -f='${Status}\t${Version}\t${Architecture}' "$package") ||
            fail "无法读取 $package 的最终配置状态。"
        IFS=$'\t' read -r status version arch <<<"$record"
        [[ $status == 'install ok installed' ]] ||
            fail "$package 在 APT 成功事务后没有完成配置：$status"
    done
}

assert_no_configure_pending_override() {
    if /usr/bin/grep -Fq 'DPkg::ConfigurePending=false' "$trace_file"; then
        fail 'APT 事务仍禁用了 pending 软件包配置。'
    fi
}

trace_has_line() {
    local prefix=$1 fragment line matched
    shift

    while IFS= read -r line; do
        [[ $line == "$prefix"* ]] || continue
        matched=1
        for fragment in "$@"; do
            if [[ $line != *"$fragment"* ]]; then
                matched=0
                break
            fi
        done
        ((matched)) && return 0
    done <"$trace_file"
    return 1
}

expect_failed_transaction_recovered() {
    local action=$1 stem=$2 label=$3 real_count

    if "$manager" "$action" >"$test_root/$stem.out" \
        2>"$test_root/$stem.err"; then
        fail "$label 被意外报告为成功。"
    fi
    grep -Fq '已恢复 Ubuntu 官方包' "$test_root/$stem.err" ||
        fail "$label 未自动恢复 Ubuntu 官方包：$(<"$test_root/$stem.err")"
    expect_status eligible official-supported-base
    assert_all_packages_configured
    grep -Fxq 'action=recovery' \
        "$sysroot/var/lib/uu-remote-for-linux/mutter-fix/state" ||
        fail "$label 没有记录 recovery 结果。"
    real_count=$(grep -c '^apt-get -y ' "$trace_file" || true)
    ((real_count >= 2)) || fail "$label 没有执行事务和恢复事务。"
    trace_has_line 'apt-get -s ' 'preinvoke-known' \
        "preinstall-$base_version" \
        '--allow-downgrades' '--reinstall' \
        ' install ' ||
        fail "$label 的恢复没有使用 --reinstall 模拟。"
    trace_has_line 'apt-get -y ' 'preinvoke-known' \
        "preinstall-$base_version" \
        '--allow-downgrades' '--reinstall' \
        ' install ' ||
        fail "$label 的恢复没有使用 --reinstall 事务。"
}

expect_preinstall_plan_rejected() {
    local mode=$1 stem=$2 label=$3

    reset_fixture
    export UU_REMOTE_MUTTER_FIXTURE_PREINSTALL_PLAN=$mode
    if "$manager" install >"$test_root/$stem.out" \
        2>"$test_root/$stem.err"; then
        fail "$label 被意外允许安装。"
    fi
    grep -Fq '安装计划未通过安全门禁；系统未被修改' \
        "$test_root/$stem.err" || fail "$label 没有以零恢复状态退出。"
    expect_status eligible official-supported-base
    /usr/bin/cmp -s -- "$base_library" "$installed_library" ||
        fail "$label 后系统库发生变化。"
    if grep -q '^fixture-write ' "$trace_file"; then
        fail "$label 没有在 fixture-write 前停止。"
    fi
    ! grep -q -- '--reinstall' "$trace_file" ||
        fail "$label 在无 transaction marker 时仍误触发 APT recovery。"
    trace_has_line 'apt-get -y ' 'preinvoke-base' \
        "preinstall-$target_version" \
        ' install ' ||
        fail "$label 的真实事务缺少 Pre-Install-Pkgs 门禁。"
}

/usr/bin/mkdir -p -- \
    "$sysroot/etc" "${installed_library%/*}" "$fake_bin" "$profile"
printf 'mock Ubuntu official Mutter library\n' >"$base_library"
printf 'mock UU Remote patched Mutter library\n' >"$target_library"
printf 'mock legacy UU Remote Mutter library\n' >"$legacy_library"
printf 'mock unrelated package\n' >"$extra_deb"
printf 'mock wrong-version Mutter package\n' >"$wrong_deb"

for command in dpkg-query dpkg dpkg-deb apt-get apt-mark flock pgrep; do
    /usr/bin/ln -s -- "$project_root/tests/mutter-fix-manager.sh" \
        "$fake_bin/$command"
done
for command in sha256sum stat readlink find install; do
    /usr/bin/ln -s -- "/usr/bin/$command" "$fake_bin/$command"
done

export UU_REMOTE_MUTTER_FIX_TEST_MODE=1
export UU_REMOTE_MUTTER_FIX_TEST_ROOT="$sysroot"
export UU_REMOTE_MUTTER_FIX_TEST_BIN="$fake_bin"
export UU_REMOTE_MUTTER_FIX_TEST_PROFILE="$profile"
export UU_REMOTE_MUTTER_FIXTURE_VERSIONS="$versions_file"
export UU_REMOTE_MUTTER_FIXTURE_PACKAGE_STATUS_FILE="$package_status_file"
export UU_REMOTE_MUTTER_FIXTURE_HOLDS="$holds_file"
export UU_REMOTE_MUTTER_FIXTURE_TRACE="$trace_file"
export UU_REMOTE_MUTTER_FIXTURE_BASE_VERSION="$base_version"
export UU_REMOTE_MUTTER_FIXTURE_TARGET_VERSION="$target_version"
export UU_REMOTE_MUTTER_FIXTURE_BASE_LIBRARY="$base_library"
export UU_REMOTE_MUTTER_FIXTURE_TARGET_LIBRARY="$target_library"
export UU_REMOTE_MUTTER_FIXTURE_INSTALLED_LIBRARY="$installed_library"
export UU_REMOTE_MUTTER_FIXTURE_APT_ONCE="$apt_once_file"
export UU_REMOTE_MUTTER_FIXTURE_PREINVOKE_ONCE="$preinvoke_once_file"
export UU_REMOTE_MUTTER_FIXTURE_SHELL_PID="$shell_pid"
export UU_REMOTE_MUTTER_FIXTURE_MANAGER="$manager"
export UU_REMOTE_MUTTER_FIXTURE_FUTURE_VERSION="$future_version"
export UU_REMOTE_MUTTER_FIXTURE_EXTRA_DEB="$extra_deb"
export UU_REMOTE_MUTTER_FIXTURE_WRONG_DEB="$wrong_deb"
export UU_REMOTE_MUTTER_FIXTURE_MUTATION_FILE=\
"$profile/packages/mutter-common_${target_version}_all.deb"

reset_fixture
expect_status eligible official-supported-base

set_versions "$target_version"
/usr/bin/cp -- "$target_library" "$installed_library"
expect_status installed-pending-logout logout-required
run_status | grep -Fxq 'restart_required=1' ||
    fail '已安装状态没有要求注销。'

for rejected_version in \
    '999:46.2-1ubuntu0.24.04.16+uuremote3' \
    '46.2-1ubuntu0.24.04.17+uuremote3' \
    '46.2-1ubuntu0.24.04.16+0uuremote4'; do
    set_versions "$rejected_version"
    /usr/bin/cp -- "$target_library" "$installed_library"
    expect_status superseded-or-unknown newer-or-third-party-mutter-detected
    if "$manager" rollback >"$test_root/unexpected-rollback.out" \
        2>"$test_root/unexpected-rollback.err"; then
        fail "未知版本 $rejected_version 被意外允许回滚。"
    fi
done

reset_fixture
set_stale_shell_mapping
expect_status rolled-back-pending-logout \
    official-library-installed-logout-required
/usr/bin/grep -Fq \
    'install:eligible|install:rolled-back-pending-logout|' "$frontend" ||
    fail '用户态管理器没有放行 rolled-back-pending-logout 安装。'
"$manager" install >"$test_root/stale-shell-install.out" \
    2>"$test_root/stale-shell-install.err" ||
    fail "Shell 映射旧库时安装失败：$(<"$test_root/stale-shell-install.err")"
grep -Fq 'Mutter 低延迟修复已安装' \
    "$test_root/stale-shell-install.out" ||
    fail 'Shell 映射旧库时缺少安装成功消息。'
expect_status installed-pending-logout logout-required
assert_all_packages_configured
trace_has_line 'apt-get -y ' 'preinvoke-base' \
    "preinstall-$target_version" ' install ' ||
    fail 'Shell 映射旧库时没有按 base 前置条件安装 target。'
[[ $(grep -c '^apt-get -y ' "$trace_file") == 1 ]] ||
    fail 'Shell 映射旧库时执行了预期外的第二次真实 APT 事务。'
assert_no_configure_pending_override

reset_fixture
{
    printf 'mutter-common\t%s\n' "$base_version"
    printf 'mutter-common-bin\t%s\n' "$target_version"
    printf 'libmutter-14-0\t%s\n' "$base_version"
    printf 'gir1.2-mutter-14\t%s\n' "$base_version"
} >"$versions_file"
expect_status recoverable-partial known-mutter-versions-skewed
"$manager" rollback >"$test_root/mixed-recovery.out" \
    2>"$test_root/mixed-recovery.err" ||
    fail "已知 base/target 混装恢复失败：$(<"$test_root/mixed-recovery.err")"
expect_status eligible official-supported-base
assert_all_packages_configured

reset_fixture
fixture_set_package_status interrupted
expect_status interrupted-known-partial \
    known-mutter-transaction-interrupted-manual-recovery-required
if "$manager" install >"$test_root/interrupted-install.out" \
    2>"$test_root/interrupted-install.err"; then
    fail '中断的已知 dpkg 状态被意外允许安装。'
fi
if "$manager" rollback >"$test_root/interrupted-rollback.out" \
    2>"$test_root/interrupted-rollback.err"; then
    fail '中断的已知 dpkg 状态被意外自动回滚。'
fi
grep -Fq '当前状态 interrupted-known-partial 不允许安装' \
    "$test_root/interrupted-install.err" ||
    fail '中断事务安装拒绝原因不明确。'
grep -Fq '当前状态 interrupted-known-partial 不允许回滚' \
    "$test_root/interrupted-rollback.err" ||
    fail '中断事务回滚拒绝原因不明确。'
if grep -q '^apt-get ' "$trace_file"; then
    fail '中断的已知 dpkg 状态触发了自动 APT 操作。'
fi
grep -Fq '请按 README 的 TTY 固定路径命令恢复 4 个已校验的官方包' \
    "$frontend" || fail '用户态管理器缺少 interrupted 状态的 TTY 恢复指引。'
if grep -q 'dpkg --configure -a' \
    "$test_root/interrupted-install.err" \
    "$test_root/interrupted-rollback.err"; then
    fail '中断事务被泛化为自动 dpkg --configure -a。'
fi

reset_fixture
{
    printf 'mutter-common\t%s\n' "$base_version"
    printf 'mutter-common-bin\t%s\n' "$target_version"
    printf 'libmutter-14-0\t%s\n' '46.2-1ubuntu0.24.04.17'
    printf 'gir1.2-mutter-14\t%s\n' "$base_version"
} >"$versions_file"
expect_status partial mutter-package-version-skew
if "$manager" rollback >"$test_root/future-mixed.out" \
    2>"$test_root/future-mixed.err"; then
    fail '混入 Ubuntu .17 的版本错配被意外允许回滚。'
fi
grep -Fq '当前状态 partial 不允许回滚' "$test_root/future-mixed.err" ||
    fail '混入 .17 的回滚拒绝原因不明确。'
assert_no_real_apt '混入 .17 的回滚拒绝'

for corrupt_version in "$target_version" "$legacy_version"; do
    reset_fixture
    set_versions "$corrupt_version"
    /usr/bin/cp -- "$base_library" "$installed_library"
    expect_status installed-corrupt target-library-hash-mismatch
    "$manager" rollback >"$test_root/corrupt-recovery.out" \
        2>"$test_root/corrupt-recovery.err" ||
        fail "错误库哈希 $corrupt_version 的恢复失败：$(<"$test_root/corrupt-recovery.err")"
    expect_status eligible official-supported-base
    assert_all_packages_configured
done

reset_fixture
write_os_release fedora 42 adams
expect_status unsupported-platform requires-ubuntu-24.04-noble-amd64

reset_fixture
printf 'libmutter-14-0\n' >"$holds_file"
expect_status held mutter-package-held
if "$manager" install >"$test_root/held.out" 2>"$test_root/held.err"; then
    fail '被 hold 的 Mutter 包被意外允许安装。'
fi
grep -Fq '当前状态 held 不允许安装' "$test_root/held.err" ||
    fail 'hold 拒绝原因不明确。'
if grep -q '^apt-get .* install ' "$trace_file"; then
    fail 'hold 拒绝后仍运行了 APT 安装事务。'
fi

reset_fixture
export UU_REMOTE_MUTTER_FIXTURE_DPKG_QUERY_GLOBAL=fail
expect_status health-check-failed multiarch-query-failed
if "$manager" install >"$test_root/global-query.out" \
    2>"$test_root/global-query.err"; then
    fail '全局 multiarch 查询失败后仍允许安装。'
fi
grep -Fq '当前状态 health-check-failed 不允许安装' \
    "$test_root/global-query.err" || fail '全局查询失败没有 fail closed。'
assert_no_real_apt '全局 multiarch 查询失败'

reset_fixture
export UU_REMOTE_MUTTER_FIXTURE_SHOWHOLD=fail
expect_status health-check-failed hold-query-failed
if "$manager" install >"$test_root/showhold.out" \
    2>"$test_root/showhold.err"; then
    fail 'apt-mark showhold 非零后仍允许安装。'
fi
grep -Fq '当前状态 health-check-failed 不允许安装' \
    "$test_root/showhold.err" || fail 'showhold 查询失败没有 fail closed。'
assert_no_real_apt 'apt-mark showhold 失败'

reset_fixture
export UU_REMOTE_MUTTER_FIXTURE_SHOWAUTO=fail
if "$manager" install >"$test_root/showauto.out" \
    2>"$test_root/showauto.err"; then
    fail 'apt-mark showauto 非零后仍允许安装。'
fi
grep -Fq '无法读取并保存原有 apt 自动/手动标记' \
    "$test_root/showauto.err" || fail 'showauto 查询失败没有 fail closed。'
assert_no_real_apt 'apt-mark showauto 失败'

reset_fixture
export UU_REMOTE_MUTTER_FIXTURE_AUDIT=fail
if "$manager" install >"$test_root/audit.out" 2>"$test_root/audit.err"; then
    fail 'dpkg --audit 非零后仍允许安装。'
fi
grep -Fq 'dpkg 或 APT 依赖状态异常' "$test_root/audit.err" ||
    fail 'dpkg --audit 非零没有 fail closed。'
assert_no_real_apt 'dpkg --audit 失败'

reset_fixture
printf 'tampered\n' >>"$profile/packages/mutter-common_${target_version}_all.deb"
expect_payload_corrupt '载荷哈希损坏'

reset_fixture
/usr/bin/grep -Fv -- \
    "  packages/mutter-common_${target_version}_all.deb" \
    "$profile/SHA256SUMS" >"$profile/SHA256SUMS.missing"
/usr/bin/mv -f -- "$profile/SHA256SUMS.missing" "$profile/SHA256SUMS"
expect_payload_corrupt 'SHA256SUMS 缺少固定 deb 项'

reset_fixture
export UU_REMOTE_MUTTER_FIXTURE_APT_SIMULATION=unexpected-remove
if "$manager" install >"$test_root/simulation.out" \
    2>"$test_root/simulation.err"; then
    fail '包含移除操作的 APT 模拟被意外接受。'
fi
grep -Fq 'APT install 模拟包含预期外的软件包变化' \
    "$test_root/simulation.err" || fail 'APT 模拟拒绝原因不明确。'
if grep -q '^apt-get -y ' "$trace_file"; then
    fail 'APT 模拟拒绝后仍执行了真实事务。'
fi
set_versions "$base_version"
/usr/bin/cp -- "$base_library" "$installed_library"
expect_status eligible official-supported-base

reset_fixture
set_versions "$legacy_version"
/usr/bin/cp -- "$legacy_library" "$installed_library"
expect_status legacy-installed-pending-logout \
    legacy-patch-normalization-and-logout-required
export UU_REMOTE_MUTTER_FIXTURE_APT_SIMULATION=unexpected-remove
if "$manager" install >"$test_root/normalize-reject.out" \
    2>"$test_root/normalize-reject.err"; then
    fail '含移除操作的 legacy 规范化模拟被意外接受。'
fi
grep -Fq '系统未被修改' "$test_root/normalize-reject.err" ||
    fail 'legacy 规范化模拟拒绝未明确零写入。'
expect_status legacy-installed-pending-logout \
    legacy-patch-normalization-and-logout-required
/usr/bin/cmp -s -- "$legacy_library" "$installed_library" ||
    fail 'legacy 规范化模拟被拒后库文件发生变化。'
assert_no_real_apt 'legacy 规范化模拟拒绝'
if grep -q -- '--reinstall' "$trace_file"; then
    fail 'legacy 规范化模拟拒绝后意外进入 recovery。'
fi

reset_fixture
set_versions "$legacy_version"
/usr/bin/cp -- "$legacy_library" "$installed_library"
expect_status legacy-installed-pending-logout \
    legacy-patch-normalization-and-logout-required
"$manager" install >"$test_root/normalize.out" \
    2>"$test_root/normalize.err" ||
    fail "legacy 规范化失败：$(<"$test_root/normalize.err")"
expect_status installed-pending-logout logout-required
assert_all_packages_configured
trace_has_line 'apt-get -s ' 'preinvoke-legacy' \
    "preinstall-$target_version" \
    '--allow-downgrades' ' install ' ||
    fail 'legacy 规范化未先执行允许降级的 APT 模拟。'
trace_has_line 'apt-get -y ' 'preinvoke-legacy' \
    "preinstall-$target_version" \
    '--allow-downgrades' ' install ' ||
    fail 'legacy 规范化未执行允许降级事务。'
assert_no_configure_pending_override

expect_preinstall_plan_rejected extra preinstall-extra \
    'Pre-Install-Pkgs 计划含第五个 deb'
expect_preinstall_plan_rejected wrong-version preinstall-wrong-version \
    'Pre-Install-Pkgs 计划含错误版本'

reset_fixture
export UU_REMOTE_MUTTER_FIXTURE_APT_REAL_MODE=target-no-write-fail
if "$manager" install >"$test_root/marker-no-write.out" \
    2>"$test_root/marker-no-write.err"; then
    fail '已有 transaction marker 的零写入 APT 失败被意外报告成功。'
fi
grep -Fq '安装计划未通过安全门禁；系统未被修改' \
    "$test_root/marker-no-write.err" ||
    fail '原 precondition/health 未变时仍触发了多余恢复。'
expect_status eligible official-supported-base
if grep -q '^fixture-write ' "$trace_file"; then
    fail '零写入 APT 失败测试意外修改了 fixture。'
fi
if grep -q -- '--reinstall' "$trace_file"; then
    fail '零写入 APT 失败在原状态健康时仍触发 recovery。'
fi
[[ ! -e $transaction_marker ]] ||
    fail 'APT 失败后遗留 transaction marker。'
trace_has_line 'apt-get -y ' 'preinvoke-base' \
    "preinstall-$target_version" \
    ' install ' ||
    fail '零写入 APT 失败没有经过 Pre-Install-Pkgs。'

reset_fixture
export UU_REMOTE_MUTTER_FIXTURE_APT_REAL_MODE=target-success-wrong
expect_failed_transaction_recovered install wrong-postverify \
    'APT 返回 0 但安装后库哈希错误'

reset_fixture
export UU_REMOTE_MUTTER_FIXTURE_APT_REAL_MODE=target-partial-fail
expect_failed_transaction_recovered install install-partial \
    '安装事务部分写入后返回非零'

reset_fixture
set_versions "$target_version"
/usr/bin/cp -- "$target_library" "$installed_library"
export UU_REMOTE_MUTTER_FIXTURE_APT_REAL_MODE=rollback-partial-fail-once
expect_failed_transaction_recovered rollback rollback-partial \
    '回滚事务部分写入后返回非零'

reset_fixture
export UU_REMOTE_MUTTER_FIXTURE_PREINVOKE_MUTATION=audit-dirty
if "$manager" install >"$test_root/preinvoke-audit.out" \
    2>"$test_root/preinvoke-audit.err"; then
    fail 'APT 模拟后 dpkg audit 变脏时仍报告安装成功。'
fi
grep -Fq '安装计划未通过安全门禁；系统未被修改' \
    "$test_root/preinvoke-audit.err" ||
    fail 'real Pre-Invoke audit 门禁拒绝未保持零恢复。'
grep -Fq '检测到未完成的 dpkg 事务' \
    "$sysroot/var/lib/uu-remote-for-linux/mutter-fix/install.log" ||
    fail 'real Pre-Invoke 未实际复核 dpkg --audit。'
expect_status eligible official-supported-base
/usr/bin/cmp -s -- "$base_library" "$installed_library" ||
    fail 'real Pre-Invoke audit 拒绝后系统库发生变化。'
if grep -q '^fixture-write ' "$trace_file"; then
    fail 'real Pre-Invoke audit 拒绝后仍执行 fixture-write。'
fi
if grep -q 'preinvoke-known\|--reinstall' "$trace_file"; then
    fail '无 transaction marker 的 audit 门禁拒绝后仍触发 recovery。'
fi
[[ ! -e $transaction_marker ]] ||
    fail 'real Pre-Invoke audit 拒绝后遗留 transaction marker。'
trace_has_line 'apt-get -y ' 'preinvoke-base' \
    "preinstall-$target_version" ' install ' ||
    fail 'audit 门禁事务缺少 Pre-Invoke/Pre-Install-Pkgs 断言。'
assert_no_configure_pending_override

reset_fixture
export UU_REMOTE_MUTTER_FIXTURE_PREINVOKE_MUTATION=future
if "$manager" install >"$test_root/preinvoke-race.out" \
    2>"$test_root/preinvoke-race.err"; then
    fail 'APT 模拟后并发升级到 .17 时仍报告安装成功。'
fi
grep -Fq '安装计划未通过安全门禁；系统未被修改' \
    "$test_root/preinvoke-race.err" ||
    fail '无 transaction marker 的 Pre-Invoke 拒绝未保持零恢复。'
expect_status superseded-or-unknown newer-or-third-party-mutter-detected
/usr/bin/cmp -s -- "$target_library" "$installed_library" ||
    fail 'APT Pre-Invoke 拒绝并发 .17 后仍降级了库文件。'
if grep -q '^fixture-write ' "$trace_file"; then
    fail 'APT Pre-Invoke 拒绝并发 .17 后 fake dpkg 仍执行了写入。'
fi
trace_has_line 'apt-get -y ' 'preinvoke-base' \
    "preinstall-$target_version" \
    ' install ' ||
    fail '初始事务没有执行 base Pre-Invoke 断言。'
if grep -q 'preinvoke-known\|--reinstall' "$trace_file"; then
    fail '无 transaction marker 的并发 .17 门禁拒绝后仍触发 recovery。'
fi

reset_fixture
export UU_REMOTE_MUTTER_FIXTURE_FLOCK_MUTATION=state-to-target
"$manager" install >"$test_root/post-lock-state.out" \
    2>"$test_root/post-lock-state.err" ||
    fail "锁后状态变化处理失败：$(<"$test_root/post-lock-state.err")"
grep -Fq 'Mutter 修复已经安装' "$test_root/post-lock-state.out" ||
    fail '锁后 eligible 到 target 变化未被重新分类。'
expect_status installed-pending-logout logout-required
assert_no_real_apt '锁后状态重新分类'

reset_fixture
export UU_REMOTE_MUTTER_FIXTURE_FLOCK_MUTATION=corrupt-payload
if "$manager" install >"$test_root/post-lock-payload.out" \
    2>"$test_root/post-lock-payload.err"; then
    fail 'flock 后被修改的 payload 被意外允许安装。'
fi
grep -Fq '修复载荷、元数据、哈希或 root 所有权校验失败' \
    "$test_root/post-lock-payload.err" ||
    fail 'flock 后 payload 变化未被重新校验。'
assert_no_real_apt 'flock 后 payload 变化'
expect_payload_corrupt 'flock 后 payload 变化'

reset_fixture
"$manager" install >"$test_root/install.out" 2>"$test_root/install.err" ||
    fail "合法安装事务失败：$(<"$test_root/install.err")"
grep -Fq 'Mutter 低延迟修复已安装' "$test_root/install.out" ||
    fail '安装成功消息缺失。'
expect_status installed-pending-logout logout-required
assert_all_packages_configured
grep -Fxq 'action=install' \
    "$sysroot/var/lib/uu-remote-for-linux/mutter-fix/state" ||
    fail '安装结果状态未落盘。'

"$manager" rollback >"$test_root/rollback.out" 2>"$test_root/rollback.err" ||
    fail "合法回滚事务失败：$(<"$test_root/rollback.err")"
grep -Fq 'Ubuntu 官方 Mutter 已恢复' "$test_root/rollback.out" ||
    fail '回滚成功消息缺失。'
expect_status eligible official-supported-base
assert_all_packages_configured
grep -Fxq 'action=rollback' \
    "$sysroot/var/lib/uu-remote-for-linux/mutter-fix/state" ||
    fail '回滚结果状态未落盘。'
trace_has_line 'apt-get -s ' 'preinvoke-public' \
    "preinstall-$base_version" \
    '--allow-downgrades' ' install ' ||
    fail '回滚没有先执行允许降级的 APT 模拟。'
trace_has_line 'apt-get -y ' 'preinvoke-public' \
    "preinstall-$base_version" \
    '--allow-downgrades' ' install ' ||
    fail '回滚没有执行允许降级的 APT 事务。'
assert_no_configure_pending_override

printf 'Mutter 修复管理器隔离测试通过。\n'
