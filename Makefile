# The build. The game comes from the copy of weemitt in weemitt/ and is built by its own Makefile, at 64 bits only.
# The game's output goes to weemitt/build/; the rest goes to build/.

VERSION ?= dev
JOBS ?= $(shell nproc 2>/dev/null || echo 4)
WEEMITT := $(MAKE) -C weemitt -j$(JOBS)

.PHONY: game windows headless crossbuild-build crossbuild regress server test image

# The game's window program for Linux: weemitt/build/linux64/weemitt (needs SDL2).
game:
	$(WEEMITT) linux64

# The game's window program for Windows: weemitt/build/windows64/weemitt.exe (needs MinGW-w64 and SDL2 for MinGW,
# weemitt/docs/building.md).
windows:
	$(WEEMITT) windows64

# The test build without a window: weemitt/build/headless64/weemitt.
headless:
	$(WEEMITT) headless64

# What tests/rom/crossbuild.py runs: the Linux headless build, and the Windows library with its test host.
crossbuild-build: headless
	$(WEEMITT) lib-windows64-stub
	$(WEEMITT) libhost-windows64

# The Linux and Windows builds give the same state and memory hash every frame (needs the ROM and Wine).
crossbuild: crossbuild-build
	python3 tests/rom/crossbuild.py

# The game's logic against g64coopdx's baseline (needs the ROM).
regress:
	cd weemitt && python3 tools/regress.py check

# The lobby server: build/lobby
server:
	cd server && CGO_ENABLED=0 go build -trimpath -ldflags "-X main.version=$(VERSION)" -o ../build/lobby .

# Every test that needs no ROM.
test:
	cd server && test -z "$$(gofmt -l .)" && go vet ./... && go test -race ./...

# The lobby server's container image.
image:
	docker build --build-arg VERSION=$(VERSION) -t g64coopdx-lobby server
