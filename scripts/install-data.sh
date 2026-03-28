#!/usr/bin/env bash

set -euo pipefail

DATA_ROOT="${1:-}"

if [[ -z "${DATA_ROOT}" ]]; then
    echo "usage: $0 <data-root>" >&2
    exit 1
fi

mkdir -p \
    "${DATA_ROOT}/models/face_detection_yunet" \
    "${DATA_ROOT}/models/face_landmark" \
    "${DATA_ROOT}/test-images/face_detection/opencv_extra" \
    "${DATA_ROOT}/test-images/face_detection/opencv_zoo"

checksum_file() {
    local algorithm="$1"
    local file="$2"

    case "${algorithm}" in
        sha1)
            sha1sum "${file}" | awk '{print $1}'
            ;;
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

# Test images for detection-stage smoke tests.
download_file \
    "${DATA_ROOT}/test-images/face_detection/opencv_extra/gray_face.png" \
    "sha256" \
    "6df8d32e7771e2a5fa9a7f00d8d32d1e69e969ce907b5825920bbdb6962d8a45" \
    "https://raw.githubusercontent.com/opencv/opencv_extra/4.x/testdata/dnn/gray_face.png"

download_file \
    "${DATA_ROOT}/test-images/face_detection/opencv_extra/dog416.png" \
    "sha256" \
    "61db162464c0770138c5136af0e999ce3b6556244cec257945427396480d9caf" \
    "https://raw.githubusercontent.com/opencv/opencv_extra/4.x/testdata/dnn/dog416.png"

download_file \
    "${DATA_ROOT}/test-images/face_detection/opencv_extra/grace_hopper_227.png" \
    "sha256" \
    "f9f5173e165918db972a36b61f73d9548f43a9859ebbcc4b5603c887f5bb6e44" \
    "https://raw.githubusercontent.com/opencv/opencv_extra/4.x/testdata/dnn/grace_hopper_227.png"

download_file \
    "${DATA_ROOT}/test-images/face_detection/opencv_extra/lena_starry_night.png" \
    "sha256" \
    "cceb68a3c897a351e3bbb7da9fe7c656f3e6344cdd2432d7ac3463626ddbe2ff" \
    "https://raw.githubusercontent.com/opencv/opencv_extra/4.x/testdata/dnn/lena_starry_night.png"

face_detection_zip="${DATA_ROOT}/test-images/face_detection/opencv_zoo/face_detection.zip"
if [[ ! -f "${DATA_ROOT}/test-images/face_detection/opencv_zoo/group.jpg" ||
      ! -f "${DATA_ROOT}/test-images/face_detection/opencv_zoo/concerts.jpg" ||
      ! -f "${DATA_ROOT}/test-images/face_detection/opencv_zoo/dance.jpg" ]]; then
    download_file_from_url \
        "https://drive.google.com/u/0/uc?id=1lOAliAIeOv4olM65YDzE55kn6XjiX2l6&export=download" \
        "${face_detection_zip}" \
        "sha1" \
        "0ba67a9cfd60f7fdb65cdb7c55a1ce76c1193df1"

    tmp_extract_dir="$(mktemp -d)"
    unzip -oq "${face_detection_zip}" -d "${tmp_extract_dir}"
    if [[ ! -d "${tmp_extract_dir}/face_detection" ]]; then
        rm -rf "${tmp_extract_dir}" "${face_detection_zip}"
        echo "unexpected layout in face_detection.zip" >&2
        exit 1
    fi

    rm -rf "${DATA_ROOT}/test-images/face_detection/opencv_zoo"
    mv "${tmp_extract_dir}/face_detection" \
        "${DATA_ROOT}/test-images/face_detection/opencv_zoo"
    rm -rf "${tmp_extract_dir}" "${face_detection_zip}"
fi

# YuNet face detector weights used by DetectorModel.
download_file \
    "${DATA_ROOT}/models/face_detection_yunet/face_detection_yunet_2022mar.onnx" \
    "sha256" \
    "50ef07f702a31741ca46a4c0d947773b64143b9362780237bf0d427d6c79bab7" \
    "https://github.com/opencv/opencv_zoo/blob/91fb0290f50896f38a0ab1e558b74b16bc009428/models/face_detection_yunet/face_detection_yunet_2022mar.onnx?raw=true"

# Face landmark model for the landmark stage.
# OpenCV's FacemarkLBF accepts the pre-trained LBF YAML model.
download_file \
    "${DATA_ROOT}/models/face_landmark/lbfmodel.yaml" \
    "sha256" \
    "70dd8b1657c42d1595d6bd13d97d932877b3bed54a95d3c4733a0f740d1fd66b" \
    "https://github.com/kurnianggoro/GSOC2017/raw/master/data/lbfmodel.yaml" \
    "https://raw.githubusercontent.com/kurnianggoro/GSOC2017/master/data/lbfmodel.yaml"

# Keep the legacy filename around so the current stage can fall back to it
# while the YAML file remains the primary visible artifact in data/.
if [[ -f "${DATA_ROOT}/models/face_landmark/lbfmodel.yaml" ]]; then
    ln -sf "lbfmodel.yaml" "${DATA_ROOT}/models/face_landmark/face_landmark_model.dat"
fi
