SHELL := /usr/bin/env bash

.PHONY: all shim nvdec-probe dxgi-probe nvenc-probe nvencode-bridge update-blocker input-hook frame-helper pipewire-cursor powershell-bridge mutter-bundle test deb clean

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

clean:
	@if [[ -d dist ]]; then find dist -mindepth 1 -maxdepth 1 -type f -name '*.deb' -delete; fi
