#!/usr/bin/env bash

set -euo pipefail

DATA_ROOT="${1:-}"

if [[ -z "${DATA_ROOT}" ]]; then
    echo "usage: $0 <data-root>" >&2
    exit 1
fi

mkdir -p \
    "${DATA_ROOT}/models/face_detection_yunet" \
    "${DATA_ROOT}/models/face_landmark"

checksum_file() {
    local algorithm="$1"
    local file="$2"

    case "${algorithm}" in
        sha256)
            sha256sum "${file}" | awk '{print $1}'
            ;;
        md5)
            md5sum "${file}" | awk '{print $1}'
            ;;
        *)
            echo "unsupported checksum algorithm: ${algorithm}" >&2
            exit 1
            ;;
    esac
}

download_file_from_url() {
    local url="$1"
    local destination="$2"
    local checksum_algorithm="${3:-sha256}"
    local expected_checksum="${4:-}"
    local tmp_file="${destination}.tmp"

    curl -fsSL --retry 3 --retry-delay 2 "${url}" -o "${tmp_file}" || {
        rm -f "${tmp_file}"
        return 1
    }

    if [[ -n "${expected_checksum}" ]]; then
        local downloaded_checksum
        downloaded_checksum="$(checksum_file "${checksum_algorithm}" "${tmp_file}")"
        if [[ "${downloaded_checksum}" != "${expected_checksum}" ]]; then
            rm -f "${tmp_file}"
            echo "${checksum_algorithm} mismatch for ${destination} from ${url}" >&2
            echo "expected: ${expected_checksum}" >&2
            echo "actual:   ${downloaded_checksum}" >&2
            return 1
        fi
    fi

    mv "${tmp_file}" "${destination}"
}

download_file() {
    local destination="$1"
    local checksum_algorithm="${2:-sha256}"
    local expected_checksum="${3:-}"
    shift 3

    local urls=("$@")

    if [[ -f "${destination}" ]]; then
        if [[ -z "${expected_checksum}" ]]; then
            return
        fi

        local current_checksum
        current_checksum="$(checksum_file "${checksum_algorithm}" "${destination}")"
        if [[ "${current_checksum}" == "${expected_checksum}" ]]; then
            return
        fi
    fi

    local url
    for url in "${urls[@]}"; do
        if download_file_from_url "${url}" "${destination}" "${checksum_algorithm}" "${expected_checksum}"; then
            return
        fi
    done

    echo "failed to download ${destination} from all mirrors:" >&2
    printf '  - %s\n' "${urls[@]}" >&2
    exit 1
}

# YuNet face detector weights used by DetectorModel.
download_file \
    "${DATA_ROOT}/models/face_detection_yunet/face_detection_yunet_2023mar.onnx" \
    "sha256" \
    "8f2383e4dd3cfbb4553ea8718107fc0423210dc964f9f4280604804ed2552fa4" \
    "https://huggingface.co/opencv/face_detection_yunet/resolve/main/face_detection_yunet_2023mar.onnx"

download_file \
    "${DATA_ROOT}/models/face_detection_yunet/face_detection_yunet_2023mar_int8.onnx" \
    "sha256" \
    "321aa5a6afabf7ecc46a3d06bfab2b579dc96eb5c3be7edd365fa04502ad9294" \
    "https://huggingface.co/opencv/face_detection_yunet/resolve/main/face_detection_yunet_2023mar_int8.onnx"

download_file \
    "${DATA_ROOT}/models/face_detection_yunet/face_detection_yunet_2023mar_int8bq.onnx" \
    "sha256" \
    "49f000ec501fef24739071fc7e68267d32209045b6822c0c72dce1da25726f10" \
    "https://huggingface.co/opencv/face_detection_yunet/resolve/main/face_detection_yunet_2023mar_int8bq.onnx"

# Face landmark model for the future landmark stage.
download_file \
    "${DATA_ROOT}/models/face_landmark/face_landmark_model.dat" \
    "md5" \
    "7505c44ca4eb54b4ab1e4777cb96ac05" \
    "https://github.com/opencv/opencv_3rdparty/raw/contrib_face_alignment_20170818/face_landmark_model.dat" \
    "https://raw.githubusercontent.com/opencv/opencv_3rdparty/8afa57abc8229d611c4937165d20e2a2d9fc5a12/face_landmark_model.dat"
