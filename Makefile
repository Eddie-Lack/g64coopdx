# The build. Only the lobby server exists so far; the game's targets come with phase 0 (docs/discovery/roadmap.md).
# All output goes to build/.

VERSION ?= dev

.PHONY: server test image

# The lobby server: build/lobby
server:
	cd server && CGO_ENABLED=0 go build -trimpath -ldflags "-X main.version=$(VERSION)" -o ../build/lobby .

# Every test that needs no ROM.
test:
	cd server && test -z "$$(gofmt -l .)" && go vet ./... && go test -race ./...

# The lobby server's container image.
image:
	docker build --build-arg VERSION=$(VERSION) -t g64coopdx-lobby server
