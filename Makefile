SHELL := /usr/bin/env bash

.PHONY: all shim nvdec-probe dxgi-probe nvenc-probe nvencode-bridge update-blocker input-hook frame-helper pipewire-cursor powershell-bridge mutter-bundle test deb apt-packages apt-repository clean

all: test

shim:
	./scripts/build-wevtapi.sh

nvdec-probe:
	./scripts/build-nvdec-probe.sh

dxgi-probe:
	./scripts/build-dxgi-probe.sh

nvenc-probe:
	./scripts/build-nvenc-d3d11-probe.sh

nvencode-bridge:
	./scripts/build-nvencode-bridge.sh

update-blocker:
	./scripts/build-update-blocker.sh

input-hook:
	./scripts/build-input-hook.sh

frame-helper:
	./scripts/build-frame-helper.sh

pipewire-cursor:
	./scripts/build-pipewire-cursor.sh

powershell-bridge:
	./scripts/build-powershell-bridge.sh

mutter-bundle:
	./packaging/mutter/verify-bundle.sh

test:
	./tests/run.sh

deb:
	./packaging/build-deb.sh

apt-packages: deb
	@test -n "$(APT_PUBLIC_KEY)" || { echo 'APT_PUBLIC_KEY is required' >&2; exit 64; }
	./packaging/apt/build-packages.sh --archive-keyring "$(APT_PUBLIC_KEY)"

apt-repository:
	@test -n "$(APT_GNUPGHOME)" || { echo 'APT_GNUPGHOME is required' >&2; exit 64; }
	@test -n "$(APT_SIGNING_KEY)" || { echo 'APT_SIGNING_KEY is required' >&2; exit 64; }
	@test -n "$(APT_PASSPHRASE_FILE)" || { echo 'APT_PASSPHRASE_FILE is required' >&2; exit 64; }
	@test -n "$(APT_REPOSITORY_OUTPUT)" || { echo 'APT_REPOSITORY_OUTPUT is required' >&2; exit 64; }
	./packaging/apt/build-repository.sh \
		--output "$(APT_REPOSITORY_OUTPUT)" \
		--gnupg-home "$(APT_GNUPGHOME)" \
		--signing-key "$(APT_SIGNING_KEY)" \
		--passphrase-file "$(APT_PASSPHRASE_FILE)" \
		--expected-public-key packaging/apt/uu-remote-for-linux-archive-keyring.gpg

clean:
	@if [[ -d dist ]]; then find dist -mindepth 1 -maxdepth 1 -type f -name '*.deb' -delete; fi
