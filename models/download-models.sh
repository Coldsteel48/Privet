#!/usr/bin/env bash
# Downloads the YuNet face-detection and SFace face-recognition ONNX
# models from the OpenCV Zoo (Apache-2.0) into this directory, verifying
# each against a pinned SHA-256 — see README.md for why
# media.githubusercontent.com is used instead of a plain raw.githubusercontent.com
# link (the latter serves a Git LFS pointer file, not the model itself).
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

YUNET_URL="https://media.githubusercontent.com/media/opencv/opencv_zoo/main/models/face_detection_yunet/face_detection_yunet_2023mar.onnx"
YUNET_FILE="face_detection_yunet_2023mar.onnx"
YUNET_SHA256="8f2383e4dd3cfbb4553ea8718107fc0423210dc964f9f4280604804ed2552fa4"

SFACE_URL="https://media.githubusercontent.com/media/opencv/opencv_zoo/main/models/face_recognition_sface/face_recognition_sface_2021dec.onnx"
SFACE_FILE="face_recognition_sface_2021dec.onnx"
SFACE_SHA256="0ba9fbfa01b5270c96627c4ef784da859931e02f04419c829e83484087c34e79"

fetch_and_verify() {
    local url="$1" file="$2" expected_sha256="$3"

    if [[ -f "$file" ]]; then
        local existing_sha256
        existing_sha256="$(sha256sum "$file" | cut -d' ' -f1)"
        if [[ "$existing_sha256" == "$expected_sha256" ]]; then
            echo "OK (already present, checksum verified): $file"
            return
        fi
        echo "Existing $file has an unexpected checksum — re-downloading." >&2
    fi

    echo "Downloading $file ..."
    curl -fL --progress-bar -o "$file.tmp" "$url"

    local actual_sha256
    actual_sha256="$(sha256sum "$file.tmp" | cut -d' ' -f1)"
    if [[ "$actual_sha256" != "$expected_sha256" ]]; then
        rm -f "$file.tmp"
        echo "ERROR: checksum mismatch for $file" >&2
        echo "  expected: $expected_sha256" >&2
        echo "  actual:   $actual_sha256" >&2
        exit 1
    fi

    mv "$file.tmp" "$file"
    echo "OK (downloaded, checksum verified): $file"
}

fetch_and_verify "$YUNET_URL" "$YUNET_FILE" "$YUNET_SHA256"
fetch_and_verify "$SFACE_URL" "$SFACE_FILE" "$SFACE_SHA256"

echo
echo "Models ready in $(pwd)"
echo "Point config/facial-auth.conf.example (or /etc/facial-auth/config.conf) at:"
echo "  detector_model_path = $(pwd)/$YUNET_FILE"
echo "  embedder_model_path = $(pwd)/$SFACE_FILE"
