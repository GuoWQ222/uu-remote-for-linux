# Security Policy

## Reporting a vulnerability

Please use GitHub's private vulnerability-reporting feature for security issues
when it is available for this repository. Do not disclose an unpatched
vulnerability in a public issue.

This is an unofficial community compatibility project and does not provide
NetEase product security support. Vulnerabilities in the proprietary UU Remote
client, its service, or NetEase infrastructure should be reported through the
vendor's official security channels.

## Protect your account and devices

Before attaching diagnostics, screenshots, or logs, remove:

- remote-assistance codes and device IDs;
- account cookies, tokens, phone numbers, and verification codes;
- public IP addresses and private network topology;
- file names or screen contents containing confidential information.

Prefer the output of `uuyc-linux-controller --diagnose`, which is designed to
report wrapper state without dumping account credentials. Do not upload the
complete Wine prefix.

Supported compatibility updates are fail-closed: unknown upstream versions or
hash mismatches are deferred instead of patched. Do not bypass those checks on
a production account or machine.
