#!/usr/bin/env bash
set -euo pipefail
project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)

python3 - "$project_root/bin/uu-remote-for-linux" <<'PY_TEST'
from pathlib import Path
import os
import subprocess
import sys
import tempfile

source = Path(sys.argv[1]).read_text()
start = source.index("authenticode_subject_matches_publisher() {\n")
end = source.index("\ndownload_installer() {", start)
publisher = "NetEase (Hangzhou) Network Co., Ltd"
legacy = f"/C=CN/O={publisher}/CN=UU Remote"
rfc = r"CN=UU Remote,O=NetEase (Hangzhou) Network Co.\, Ltd,C=CN"
cases = [
    ("legacy-middle", legacy, True),
    ("legacy-end", f"/CN=UU Remote/O={publisher}", True),
    ("legacy-start", f"/O={publisher}/CN=UU Remote", True),
    ("legacy-utf8", r"/L=\xE6\x9D\xAD\xE5\xB7\x9E/O=" + publisher, True),
    ("legacy-escaped-comma", r"/O=NetEase (Hangzhou) Network Co.\, Ltd/CN=UU", True),
    ("rfc2253", rfc, True),
    ("rfc-order", r"O=NetEase (Hangzhou) Network Co.\, Ltd,CN=UU", True),
    ("rfc-hex", r"CN=UU,O=NetEase (Hangzhou) Network Co.\2C Ltd", True),
    ("rfc-hex-lowercase", r"CN=UU,O=NetEase (Hangzhou) Network Co.\2c Ltd", True),
    ("rfc-utf8", r"L=\E6\9D\AD\E5\B7\9E,O=NetEase (Hangzhou) Network Co.\, Ltd", True),
    ("rfc-unicode", r"L=杭州市,O=NetEase (Hangzhou) Network Co.\, Ltd", True),
    ("rfc-multivalued-rdn", r"CN=UU+O=NetEase (Hangzhou) Network Co.\, Ltd,C=CN", True),
    ("rfc-escaped-plus", r"CN=UU\+O=decoy,O=NetEase (Hangzhou) Network Co.\, Ltd", True),
    ("rfc-escaped-backslash", r"CN=UU\\,O=NetEase (Hangzhou) Network Co.\, Ltd", True),
    ("rfc-oid", r"CN=UU,2.5.4.10=NetEase (Hangzhou) Network Co.\, Ltd", True),
    ("rfc-oid-prefix", r"OID.2.5.4.10=NetEase (Hangzhou) Network Co.\, Ltd", True),
    ("rfc-lowercase-type", r"o=NetEase (Hangzhou) Network Co.\, Ltd", True),
    ("rfc-separator-spacing", r"CN=UU, O=NetEase (Hangzhou) Network Co.\, Ltd", True),
    ("empty", "", False),
    ("wrong-publisher", "CN=UU,O=Unrelated Publisher,C=CN", False),
    ("name-in-cn", r"CN=NetEase (Hangzhou) Network Co.\, Ltd,O=Unrelated", False),
    ("name-in-ou", r"OU=NetEase (Hangzhou) Network Co.\, Ltd,O=Unrelated", False),
    ("embedded-comma-org", r"CN=Unrelated\,O=NetEase (Hangzhou) Network Co.\, Ltd,O=Unrelated", False),
    ("embedded-plus-org", r"CN=Unrelated\+O=NetEase (Hangzhou) Network Co.\, Ltd,O=Unrelated", False),
    ("embedded-slash-org", r"/CN=Unrelated\/O=NetEase (Hangzhou) Network Co., Ltd/O=Unrelated", False),
    ("embedded-hex-org", r"CN=Unrelated\2CO=NetEase (Hangzhou) Network Co.\, Ltd,O=Unrelated", False),
    ("legacy-name-only-in-cn", f"/CN={publisher}/O=Unrelated", False),
    ("missing-org", "CN=UU,C=CN", False),
    ("duplicate-org", rfc + r",O=NetEase (Hangzhou) Network Co.\, Ltd", False),
    ("conflicting-org", rfc + ",O=Unrelated", False),
    ("duplicate-rdn-org", rfc + "+O=Unrelated", False),
    ("oid-duplicate", rfc + r",2.5.4.10=NetEase (Hangzhou) Network Co.\, Ltd", False),
    ("prefix", r"O=NetEase (Hangzhou) Network Co.\, Ltd Extra", False),
    ("suffix", r"O=Fake NetEase (Hangzhou) Network Co.\, Ltd", False),
    ("unescaped-comma", f"O={publisher}", False),
    ("trailing-escape", rfc + "\\", False),
    ("malformed-hex", r"CN=\2Z,O=NetEase (Hangzhou) Network Co.\, Ltd", False),
    ("invalid-utf8", r"CN=\FF,O=NetEase (Hangzhou) Network Co.\, Ltd", False),
    ("escaped-control", r"CN=\0A,O=NetEase (Hangzhou) Network Co.\, Ltd", False),
    ("literal-control", "CN=UU\n," + rfc, False),
    ("empty-attribute", rfc + ",", False),
    ("invalid-attribute", "bogus," + rfc, False),
    ("quoted-injection", 'CN="decoy,' + rfc + '",O=Unrelated', False),
    ("semicolon-injection", 'CN=decoy;' + rfc, False),
    ("unknown-name-format", "subject=" + legacy, False),
    ("unknown-ber-value", "O=#0C03414243", False),
]

with tempfile.TemporaryDirectory() as temp:
    root = Path(temp)
    functions = root / "signature-functions.sh"
    functions.write_text(source[start:end] + "\n")
    checked = 0
    for label, subject, accepted in cases:
        result = subprocess.run(
            ["bash", "-c", 'source "$1"; authenticode_subject_matches_publisher "$2" "$3"',
             "test", str(functions), subject, publisher], capture_output=True, text=True,
        )
        assert result.returncode == (0 if accepted else 1), (label, result.returncode, result.stderr)
        checked += 1
    # The parser is parameterized by publisher; no tool or distribution
    # version and no particular client release selects a parsing branch.
    generic = subprocess.run(
        ["bash", "-c", 'source "$1"; authenticode_subject_matches_publisher "$2" "$3"',
         "test", str(functions), "CN=WebView2,O=Microsoft Corporation,C=US", "Microsoft Corporation"],
        capture_output=True, text=True,
    )
    assert generic.returncode == 0, generic.stderr
    print(f"PASS installer publisher DN fields and escapes ({checked + 1} cases)")

    verifier = root / "verifier"
    verifier.write_text('#!/bin/bash\nprintf "%s\\n" "$*" > "$SIGNATURE_CASE_ROOT/args"\ncat -- "$SIGNATURE_CASE_OUTPUT"\nexit "$SIGNATURE_CASE_EXIT"\n')
    verifier.chmod(0o755)
    runner = root / "gate.sh"
    runner.write_text('''#!/bin/bash
set -u
source "$SIGNATURE_CASE_FUNCTIONS"
cache_dir="$SIGNATURE_CASE_ROOT"
setup_log="$SIGNATURE_CASE_ROOT/setup.log"
authenticode_verifier="$SIGNATURE_CASE_VERIFIER"
verify_official_installer_signature "$SIGNATURE_CASE_ROOT/installer.exe"
''')
    (root / "installer.exe").write_bytes(b"MZ")
    def output(subject):
        return "Signer's certificate:\n    Signer #0:\n        Subject: " + subject + "\nSignature verification: ok\nSucceeded\n"

    log_cases = [
        ("legacy-verified", output(legacy), 0, True),
        ("rfc-verified", output(rfc), 0, True),
        ("crypto-failure-despite-correct-subject", output(rfc), 1, False),
        ("verifier-error", output(rfc), 7, False),
        ("empty-verifier-output", "", 0, False),
        ("wrong-signer-decoy-certificate", output("CN=UU,O=Other") + "Certificate chain:\n    Subject: " + rfc + "\n", 0, False),
        ("absent-subject-decoy-chain", "Signer's certificate:\n    Signer #0:\n        Issuer: Other\nNumber of certificates: 1\n    Subject: " + rfc + "\n", 0, False),
        ("absent-subject-second-signer", "Signer's certificate:\n    Signer #0:\n        Issuer: Other\n    Signer #1:\n        Subject: " + rfc + "\n", 0, False),
        ("unrelated-certificate-only", "Timestamp certificate:\n    Subject: " + rfc + "\n", 0, False),
    ]
    for label, log, status, accepted in log_cases:
        log_file = root / "case.log"
        log_file.write_text(log)
        environment = dict(os.environ, SIGNATURE_CASE_OUTPUT=str(log_file), SIGNATURE_CASE_EXIT=str(status),
                           SIGNATURE_CASE_ROOT=str(root), SIGNATURE_CASE_FUNCTIONS=str(functions),
                           SIGNATURE_CASE_VERIFIER=str(verifier))
        result = subprocess.run(["bash", str(runner)], env=environment, capture_output=True, text=True)
        assert result.returncode == (0 if accepted else 1), (label, result.returncode, result.stderr)
        assert "-index 0" in (root / "args").read_text(), "verification must bind the inspected primary signer"
        assert not list(root.glob(".authenticode.*")), (label, "temporary verification output leaked")
    print(f"PASS installer signature gate and signer isolation ({len(log_cases)} cases)")
PY_TEST
