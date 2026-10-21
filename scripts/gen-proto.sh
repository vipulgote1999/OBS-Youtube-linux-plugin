#!/usr/bin/env bash
# Regenerate the Go bindings for the YouTube live-chat streaming protocol.
#
# The generated stream_list.pb.go and stream_list_grpc.pb.go are committed, so
# a normal build never needs protoc. Run this only when the .proto changes.
#
# Toolchain (pinned to the versions the committed files were generated with):
#   go install google.golang.org/protobuf/cmd/protoc-gen-go@v1.34.2
#   go install google.golang.org/grpc/cmd/protoc-gen-go-grpc@v1.5.1
#   protoc 27.2 (libprotoc 27.2)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HELPER_DIR="$(cd "${SCRIPT_DIR}/../helper" && pwd)"

readonly WANT_PROTOC_GEN_GO="v1.34.2"
readonly WANT_PROTOC_GEN_GO_GRPC="1.5.1"

require() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "error: $1 is not installed" >&2
        echo "install: $2" >&2
        exit 1
    fi
}

require protoc "protoc 27.2 (libprotoc 27.2)"
require protoc-gen-go "go install google.golang.org/protobuf/cmd/protoc-gen-go@${WANT_PROTOC_GEN_GO}"
require protoc-gen-go-grpc "go install google.golang.org/grpc/cmd/protoc-gen-go-grpc@${WANT_PROTOC_GEN_GO_GRPC}"

got_go="$(protoc-gen-go --version 2>&1)"
got_grpc="$(protoc-gen-go-grpc --version 2>&1)"
case "${got_go}" in
    *"${WANT_PROTOC_GEN_GO}") ;;
    *)
        echo "error: protoc-gen-go is ${got_go}, want ${WANT_PROTOC_GEN_GO}" >&2
        echo "install: go install google.golang.org/protobuf/cmd/protoc-gen-go@${WANT_PROTOC_GEN_GO}" >&2
        exit 1
        ;;
esac
case "${got_grpc}" in
    *"${WANT_PROTOC_GEN_GO_GRPC}") ;;
    *)
        echo "error: protoc-gen-go-grpc is ${got_grpc}, want ${WANT_PROTOC_GEN_GO_GRPC}" >&2
        echo "install: go install google.golang.org/grpc/cmd/protoc-gen-go-grpc@${WANT_PROTOC_GEN_GO_GRPC}" >&2
        exit 1
        ;;
esac

cd "${HELPER_DIR}"
protoc \
    --go_out=. --go_opt=paths=source_relative \
    --go-grpc_out=. --go-grpc_opt=paths=source_relative \
    proto/stream_list.proto

echo "generated helper/proto/stream_list.pb.go and stream_list_grpc.pb.go"
