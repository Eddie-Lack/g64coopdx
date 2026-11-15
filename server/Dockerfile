# The lobby server as a small container image. It holds one static program and nothing else.
FROM golang:1.27-alpine AS build
WORKDIR /src
COPY go.mod *.go ./
ARG VERSION=dev
RUN CGO_ENABLED=0 go build -trimpath -ldflags "-s -w -X main.version=${VERSION}" -o /lobby .

FROM scratch
COPY --from=build /lobby /lobby
# An unprivileged user id; the image has no user database.
USER 65532:65532
EXPOSE 8080
EXPOSE 27065/udp
ENTRYPOINT ["/lobby"]
