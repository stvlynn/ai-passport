#!/usr/bin/env bash
set -euo pipefail

mode="${1:---all}"
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    echo "Usage: $0 [--all|--static|--firmware]" >&2
}

run_static_checks() {
    local actionlint_bin
    local test_dir

    python3 tools/check_repo.py

    actionlint_bin="${ACTIONLINT_BIN:-}"
    if [[ -z "${actionlint_bin}" ]]; then
        actionlint_bin="$(command -v actionlint || true)"
    fi
    if [[ -z "${actionlint_bin}" || ! -x "${actionlint_bin}" ]]; then
        actionlint_bin="$(./tools/install-actionlint.sh)"
    fi
    "${actionlint_bin}" -color .github/workflows/*.yml

    test_dir="$(mktemp -d /tmp/ai-passport-host-tests.XXXXXX)"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_ui_pixel_math.c main/ui_pixel_math.c \
        -o "${test_dir}/test_ui_pixel_math"
    "${test_dir}/test_ui_pixel_math"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_demo_navigation.c main/demo_navigation.c \
        -o "${test_dir}/test_demo_navigation"
    "${test_dir}/test_demo_navigation"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_key_guard.c -o "${test_dir}/test_key_guard"
    "${test_dir}/test_key_guard"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_display_rounding.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_display_rounding"
    "${test_dir}/test_bsp_display_rounding"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Icomponents/bsp/src \
        tests/test_bsp_es8311_sleep_check.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_es8311_sleep_check"
    "${test_dir}/test_bsp_es8311_sleep_check"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/battery_stubs -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_battery.c -o "${test_dir}/test_bsp_battery"
    "${test_dir}/test_bsp_battery"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_button.c -o "${test_dir}/test_bsp_button"
    "${test_dir}/test_bsp_button"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/bsp_stubs -Icomponents/bsp/include \
        tests/test_bsp_lvgl_init.c components/bsp/src/bsp_display_rounding.c \
        -o "${test_dir}/test_bsp_lvgl_init"
    "${test_dir}/test_bsp_lvgl_init"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
        -Itests/audio_stubs -Icomponents/bsp/include -Icomponents/bsp/src \
        tests/test_bsp_audio_recovery.c components/bsp/src/bsp_es8311_sleep_check.c \
        -o "${test_dir}/test_bsp_audio_recovery"
    "${test_dir}/test_bsp_audio_recovery"
    local gc_sections="-Wl,--gc-sections"
    if [[ "$(uname -s)" == Darwin ]]; then gc_sections="-Wl,-dead_strip"; fi
    for demo in audio low_power ble wifi; do
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror \
            -ffunction-sections -fdata-sections -Itests/demo_stubs -Imain \
            "tests/test_demo_${demo}_runtime.c" "${gc_sections}" \
            -o "${test_dir}/test_demo_${demo}_runtime"
        "${test_dir}/test_demo_${demo}_runtime"
    done
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/voice_stubs -Imain \
        tests/test_app_voice_capture.c main/sound_synth.c main/voice_codec.c -o "${test_dir}/test_app_voice_capture"
    "${test_dir}/test_app_voice_capture"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_voice_codec.c main/voice_codec.c -lm -o "${test_dir}/test_voice_codec"
    "${test_dir}/test_voice_codec"
    # Astra Monitor 应用逻辑:数据模型、GraphQL 解析(用 ESP-IDF 自带 cJSON)、导航、音效合成、配网协议。
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_gh_model.c main/gh_model.c -o "${test_dir}/test_gh_model"
    "${test_dir}/test_gh_model"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_app_nav.c main/app_nav.c -o "${test_dir}/test_app_nav"
    "${test_dir}/test_app_nav"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_sound_synth.c main/sound_synth.c -o "${test_dir}/test_sound_synth"
    "${test_dir}/test_sound_synth"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_portal_proto.c main/portal_proto.c -o "${test_dir}/test_portal_proto"
    "${test_dir}/test_portal_proto"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_wifi_profiles.c main/wifi_profiles.c -o "${test_dir}/test_wifi_profiles"
    "${test_dir}/test_wifi_profiles"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/provision_stubs -Imain \
        tests/test_app_net_profiles.c main/wifi_profiles.c -o "${test_dir}/test_app_net_profiles"
    "${test_dir}/test_app_net_profiles"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
        -Itests/provision_stubs -Imain tests/test_app_portal.c main/portal_proto.c \
        "${gc_sections}" -o "${test_dir}/test_app_portal"
    "${test_dir}/test_app_portal"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
        -Itests/provision_stubs -Imain tests/test_app_settings_wifi.c main/wifi_profiles.c \
        "${gc_sections}" -o "${test_dir}/test_app_settings_wifi"
    "${test_dir}/test_app_settings_wifi"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_lynn_anim.c main/lynn_anim.c -o "${test_dir}/test_lynn_anim"
    "${test_dir}/test_lynn_anim"
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain \
        tests/test_lynn_stage.c main/lynn_stage.c main/lynn_stage_data.c -o "${test_dir}/test_lynn_stage"
    "${test_dir}/test_lynn_stage"
    cjson_dir="${IDF_PATH:-}/components/json/cJSON"
    if [[ -f "${cjson_dir}/cJSON.c" ]]; then
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain -I"${cjson_dir}" \
            tests/test_gh_parse.c main/gh_parse.c main/gh_model.c "${cjson_dir}/cJSON.c" \
            -o "${test_dir}/test_gh_parse"
        "${test_dir}/test_gh_parse" tests/fixtures/gh_graphql_sample.json
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Imain -I"${cjson_dir}" \
            tests/test_grok_proto.c main/grok_proto.c main/lynn_stage.c main/lynn_stage_data.c "${cjson_dir}/cJSON.c" \
            -o "${test_dir}/test_grok_proto"
        "${test_dir}/test_grok_proto"
        "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Itests/voice_stubs -Imain -I"${cjson_dir}" \
            tests/test_app_grok_transport.c main/grok_proto.c main/voice_codec.c main/lynn_stage.c main/lynn_stage_data.c "${cjson_dir}/cJSON.c" \
            -o "${test_dir}/test_app_grok_transport"
        "${test_dir}/test_app_grok_transport"
    else
        echo "SKIP test_gh_parse: IDF_PATH not set (needs ESP-IDF's bundled cJSON)" >&2
    fi
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_battery_profile.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_deep_sleep_contract.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_key_wake_contract.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_check_repo.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_verify_firmware.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_archive_firmware.py
    PYTHONDONTWRITEBYTECODE=1 python3 tests/test_install_passport_skills.py
    rm -rf "${test_dir}"
    echo "Host tests: PASS"
}

run_firmware_checks() (
    # The subshell already scopes this variable. On macOS Bash 3.2 a local
    # expires before the EXIT trap runs, which breaks cleanup under set -u.

    if ! command -v idf.py >/dev/null 2>&1; then
        echo "ERROR: idf.py is not available; activate ESP-IDF 5.5.3 first." >&2
        return 1
    fi

    validation_build_dir="$(mktemp -d /tmp/ai-passport-firmware.XXXXXX)"
    trap 'case "${validation_build_dir}" in /tmp/ai-passport-firmware.*) rm -rf -- "${validation_build_dir}" ;; esac' EXIT

    SDKCONFIG_DEFAULTS="${repo_root}/sdkconfig.defaults" \
        idf.py -B "${validation_build_dir}" \
        -D "SDKCONFIG=${validation_build_dir}/sdkconfig" build
    idf.py -B "${validation_build_dir}" merge-bin \
        -o "${validation_build_dir}/FoloToy-AI-Passport-full.bin"
    python3 tools/verify_firmware.py "${validation_build_dir}"
    PYTHONDONTWRITEBYTECODE=1 python3 tools/archive_firmware.py create \
        "${validation_build_dir}" --archive-root "${repo_root}/build/firmware"
    mkdir -p "${repo_root}/build"
    install -m 0644 \
        "${validation_build_dir}/FoloToy-AI-Passport-full.bin" \
        "${repo_root}/build/FoloToy-AI-Passport-full.bin"
    echo "Firmware build: PASS"
)

cd "${repo_root}"
case "${mode}" in
    --all)
        run_static_checks
        run_firmware_checks
        ;;
    --static)
        run_static_checks
        ;;
    --firmware)
        run_firmware_checks
        ;;
    *)
        usage
        exit 2
        ;;
esac
