SHELL := /usr/bin/env bash

.PHONY: all shim nvdec-probe update-blocker test deb clean

all: test

shim:
	./scripts/build-wevtapi.sh

nvdec-probe:
	./scripts/build-nvdec-probe.sh

update-blocker:
	./scripts/build-update-blocker.sh

test:
	./tests/run.sh

deb:
	./packaging/build-deb.sh

clean:
	@if [[ -d dist ]]; then find dist -mindepth 1 -maxdepth 1 -type f -name '*.deb' -delete; fi
