#!/usr/bin/env bash
# Local firmware build for Linux, equivalent to tools/build-firmware-local.ps1
# and the CI compile step in .github/workflows/firmware.yml.
#
# Usage: tools/build-firmware-local.sh --profile <name> [--define NAME[=VALUE]]...
#          [--output-dir DIR] [--build-path DIR] [--skip-tests] [--clean] [--fast]
#          [--optimize FLAGS]
#
# --optimize replaces the core's -Os for the sketch and its libraries (not the
# prebuilt ESP-IDF). -O2 measured 10-20 % faster Assist screen frames on the
# 7B for 720 KB more flash (docs-dev/assist-screens.md); it is not the
# default and the Tab5 has no room for it. Use its own --build-path.
#
# Requires arduino-cli with esp32:esp32@3.3.7 and the libraries pinned in
# sketch.yaml installed in ~/.arduino15 and ~/Arduino/libraries (see the
# "Install ESP32 core and Arduino libraries" CI step).
set -euo pipefail

core_version="3.3.7"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
arduino_cli="${ARDUINO_CLI:-$(command -v arduino-cli || echo "$HOME/.local/bin/arduino-cli")}"
arduino_esp32="$HOME/.arduino15/packages/esp32"
libraries="$HOME/Arduino/libraries"

profile=""
output_dir=""
build_path=""
skip_tests=0
clean=0
fast=0
optimize=""
extra_defines=()

while [ $# -gt 0 ]; do
  case "$1" in
    --profile) profile="${2,,}"; shift 2 ;;
    --define) extra_defines+=("$2"); shift 2 ;;
    --output-dir) output_dir="$2"; shift 2 ;;
    --build-path) build_path="$2"; shift 2 ;;
    --skip-tests) skip_tests=1; shift ;;
    --clean) clean=1; shift ;;
    --fast) fast=1; shift ;;
    --optimize) optimize="$2"; shift 2 ;;
    *) echo "Unknown argument: $1" >&2; exit 2 ;;
  esac
done
[ -n "$profile" ] || { echo "Missing --profile" >&2; exit 2; }
[ -x "$arduino_cli" ] || { echo "Arduino CLI was not found: $arduino_cli" >&2; exit 1; }
grep -qx "version=9.6.0" "$libraries/lvgl/library.properties" ||
  { echo "LVGL 9.6.0 was not found in $libraries/lvgl" >&2; exit 1; }

# CI pins Node 24. Newer Node releases ship a zlib whose gzip output differs,
# which makes the generated WebUI asset check fail. HOMETILES_NODE_DIR (default
# ~/.local/node24/bin) is preferred when it exists.
node_dir="${HOMETILES_NODE_DIR:-$HOME/.local/node24/bin}"
[ ! -x "$node_dir/node" ] || export PATH="$node_dir:$PATH"
node_major="$(node -p 'process.versions.node.split(".")[0]')"
[ "$node_major" = "24" ] ||
  { echo "Node 24 is required (found $(node --version)); set HOMETILES_NODE_DIR." >&2; exit 1; }

# The LVGL host tests use the same LVGL 9.6.0 library as the firmware.
export LVGL_INCLUDE="${LVGL_INCLUDE:-$libraries/lvgl}"
# The Admin DOM tests need Chrome; a Chrome for Testing headless shell in
# ~/.local/chrome (npx @puppeteer/browsers install chrome-headless-shell@stable
# --path ~/.local/chrome) is used when CHROME_PATH is unset.
if [ -z "${CHROME_PATH:-}" ]; then
  chrome_shell="$(find "$HOME/.local/chrome" -type f -name chrome-headless-shell -perm -u+x 2>/dev/null | sort | tail -1)"
  [ -z "$chrome_shell" ] || export CHROME_PATH="$chrome_shell"
fi

cd "$repo_root"
sketch_profiles="$repo_root/sketch.yaml"
hidden_sketch_profiles="$repo_root/sketch.yaml.hometiles-local-build"
[ ! -e "$hidden_sketch_profiles" ] ||
  { echo "Temporary profile file already exists: $hidden_sketch_profiles" >&2; exit 1; }

profile_json="$(node tools/device-catalog.js --profile "$profile")"
field() { node -e 'const p=JSON.parse(process.argv[1]); process.stdout.write(String(p[process.argv[2]] ?? ""))' "$profile_json" "$1"; }
define="$(field define)"
chip_family="$(field chipFamily)"
rx_variant="$(field rxVariant)"
elf_flags="$(field elfFlags)"

node tools/generate-device-profiles.mjs --check
node tools/generate-web-assets.mjs --check
if [ "$skip_tests" -eq 0 ]; then
  node tools/run-tests.mjs
fi

if [ -z "$build_path" ]; then
  suffix=""
  if [ ${#extra_defines[@]} -gt 0 ]; then
    suffix="-$(printf '%s\n' "${extra_defines[@]}" | sort | paste -sd, - | tr -d '\n' | sha256sum | cut -c1-8)"
  fi
  build_path="$HOME/.cache/arduino/sketches/hometiles-lvgl96-$profile$suffix"
fi
mkdir -p "$build_path"
echo "Build cache: $build_path"

fqbn="$(awk -v wanted="$profile" '
  /^  [^[:space:]][^:]*:$/ { name=$1; sub(/:$/, "", name); in_profile=(name == wanted); next }
  in_profile && /^    fqbn:/ { sub(/^    fqbn:[[:space:]]*/, ""); print; exit }
' sketch.yaml)"
[ -n "$fqbn" ] || { echo "FQBN for profile '$profile' was not found in sketch.yaml." >&2; exit 1; }

output_dir="${output_dir:-build/local-safe-$profile}"
case "$output_dir" in /*) ;; *) output_dir="$repo_root/$output_dir" ;; esac
mkdir -p "$output_dir"

# Replace archive members with the repository objects. Archives that already
# hold identical members stay untouched so compiler caches remain valid.
patch_archive() {
  local archive="$1"; shift
  local ar_bin="$1"; shift
  [ -f "$archive" ] || { echo "Archive not found: $archive" >&2; exit 1; }
  [ -f "$archive.hometiles-unpatched-backup" ] || cp "$archive" "$archive.hometiles-unpatched-backup"
  local work_dir
  work_dir="$(mktemp -d)"
  local object name changed=0
  for object in "$@"; do
    [ -f "$object" ] || { echo "Patch object not found: $object" >&2; exit 1; }
    name="$(basename "$object")"
    if ! (cd "$work_dir" && rm -f "$name" && "$ar_bin" x "$archive" "$name" 2>/dev/null && cmp -s "$name" "$object"); then
      cp "$object" "$work_dir/$name"
      "$ar_bin" rs "$archive" "$work_dir/$name"
      (cd "$work_dir" && rm "$name" && "$ar_bin" x "$archive" "$name" && cmp "$name" "$object")
      changed=1
    fi
  done
  rm -rf "$work_dir"
  if [ "$changed" -eq 1 ]; then echo "Patched $archive"; else echo "Already patched: $archive"; fi
}

if [ "$chip_family" != "ESP32-S3" ]; then
  ar_bin="$(find "$arduino_esp32/tools" -type f -path '*/bin/riscv32-esp-elf-ar' -print -quit)"
  [ -x "$ar_bin" ] || { echo "riscv32-esp-elf-ar was not found" >&2; exit 1; }
  tx_fix="$repo_root/tools/esp-hosted-3.3.7-tx-fix"
  rx_fix="$repo_root/tools/esp-hosted-3.3.7-rx-fix"
  cache_safe="$repo_root/tools/esp-idf-3.3.7-p4-cache-safe"
  for variant in esp32p4-libs esp32p4_es-libs; do
    lib_dir="$arduino_esp32/tools/$variant/$core_version/lib"
    objects=("$tx_fix/$variant"/*.obj)
    for object in "$rx_fix/$variant"/*.obj; do
      [ "$(basename "$object")" = "sdio_drv.c.obj" ] || objects+=("$object")
    done
    if [ "$rx_variant" = "repo-short-tail" ]; then
      objects+=("$rx_fix/$variant/sdio_drv.c.obj")
    else
      objects+=("$rx_fix/baseline-a8204/$variant/sdio_drv.c.obj")
    fi
    if [ "$variant" = "esp32p4_es-libs" ]; then
      port_sdio_variant="baseline-a8204"
      [ "$rx_variant" != "repo-guition-jc8012-rx-single-block" ] || port_sdio_variant="guition-jc8012-rx-single-block"
      objects+=("$rx_fix/$port_sdio_variant/$variant/port_esp_hosted_host_sdio.c.obj")
    fi
    patch_archive "$lib_dir/libespressif__esp_hosted.a" "$ar_bin" "${objects[@]}"

    # The panel keeps refreshing during flash writes (no blue flash):
    # tools/esp-idf-3.3.7-p4-cache-safe/README.md.
    for pair in esp_lcd_panel_dpi.c.obj:libesp_lcd.a dw_gdma.c.obj:libesp_hw_support.a esp_cam_ctlr_csi.c.obj:libesp_driver_cam.a; do
      patch_archive "$lib_dir/${pair##*:}" "$ar_bin" "$cache_safe/$variant/${pair%%:*}"
    done
  done
fi

# arduino-cli requires the sketch folder name to match HomeTiles.ino and
# resolves symlinks. A checkout in another folder (for example a git worktree)
# compiles from a HomeTiles mirror beside the build cache (arduino-cli owns
# the build folder itself); rsync keeps mtimes so incremental builds stay
# incremental.
sketch_dir="$repo_root"
if [ "$(basename "$repo_root")" != "HomeTiles" ]; then
  sketch_dir="$build_path.src/HomeTiles"
  [ ! -L "$sketch_dir" ] || rm "$sketch_dir"
  mkdir -p "$sketch_dir"
  rsync -a --delete --exclude=/.git --exclude=/build --exclude=/node_modules \
    --exclude=/sketch.yaml --exclude=/sketch.yaml.hometiles-local-build \
    "$repo_root/" "$sketch_dir/"
fi

common_flags="-DLV_CONF_INCLUDE_SIMPLE -I$sketch_dir -I$libraries"
extra_define_flags=""
for d in "${extra_defines[@]}"; do
  [[ "$d" =~ ^[A-Za-z_][A-Za-z0-9_]*(=.*)?$ ]] || { echo "Invalid extra compiler define: $d" >&2; exit 1; }
  extra_define_flags+=" -D$d"
done
cpp_flags="-DHOMETILES_CI_TARGET -D$define$extra_define_flags $common_flags"

# HomeTiles never throws or catches; repeat the platform.txt line and append
# -fno-exceptions after the SDK flag file. A new core must update both lines.
platform_cpp_flags='-MMD -c "@{compiler.sdk.path}/flags/cpp_flags" {compiler.warning_flags} {compiler.optimization_flags} {compiler.common_werror_flags}'
grep -Fqx "compiler.cpp.flags=$platform_cpp_flags" "$arduino_esp32/hardware/esp32/$core_version/platform.txt" ||
  { echo "compiler.cpp.flags in platform.txt changed; update the -fno-exceptions override." >&2; exit 1; }

build_args=(
  --fqbn "$fqbn"
  --build-path "$build_path"
  --build-property "compiler.c.extra_flags=$cpp_flags"
  --build-property "compiler.cpp.extra_flags=$cpp_flags"
  --build-property "compiler.cpp.flags=$platform_cpp_flags -fno-exceptions"
  --build-property "compiler.c.elf.extra_flags=$elf_flags"
)
if [ -n "$optimize" ]; then
  [[ "$optimize" =~ ^-O[0-3sz]$ ]] || { echo "Invalid --optimize (use -O0..-O3, -Os, -Oz): $optimize" >&2; exit 1; }
  build_args+=(--build-property "compiler.optimization_flags=$optimize")
fi

# arduino-cli reinstalls sketch.yaml profile dependencies (and the stock
# ESP-Hosted archive) when it sees the profile file, so hide it while compiling.
mv "$sketch_profiles" "$hidden_sketch_profiles"
trap '[ -f "$hidden_sketch_profiles" ] && mv "$hidden_sketch_profiles" "$sketch_profiles"' EXIT

fast_done=0
if [ "$fast" -eq 1 ] && [ "$clean" -eq 0 ]; then
  props_file="$build_path/hometiles-fast-props.txt"
  props_key_file="$build_path/hometiles-fast-props.key"
  props_key="$(printf '%s|' "${build_args[@]}")"
  if [ ! -f "$props_file" ] || [ "$(cat "$props_key_file" 2>/dev/null)" != "$props_key" ]; then
    "$arduino_cli" compile "${build_args[@]}" --show-properties=expanded "$sketch_dir" > "$props_file"
    printf '%s' "$props_key" > "$props_key_file"
  fi
  set +e
  node tools/fast-build.mjs --build-path "$build_path" --repo "$sketch_dir" --props "$props_file" \
    --output-dir "$output_dir" --expect-flags "$cpp_flags -fno-exceptions" --fqbn "$fqbn"
  status=$?
  set -e
  if [ $status -eq 0 ]; then fast_done=1
  elif [ $status -eq 3 ]; then echo "Falling back to the full arduino-cli build."
  else echo "Fast build failed for profile '$profile'." >&2; exit 1; fi
fi
if [ "$fast_done" -eq 0 ]; then
  clean_args=()
  [ "$clean" -eq 0 ] || clean_args+=(--clean)
  "$arduino_cli" compile "${clean_args[@]}" "${build_args[@]}" --export-binaries --output-dir "$output_dir" "$sketch_dir"
fi

mv "$hidden_sketch_profiles" "$sketch_profiles"
trap - EXIT

firmware_bin="$output_dir/HomeTiles.ino.bin"
[ -f "$firmware_bin" ] || { echo "Firmware binary was not created: $firmware_bin" >&2; exit 1; }
firmware_bytes="$(stat -c %s "$firmware_bin")"
ota_slot_bytes=$((0x680000))
[ "$firmware_bytes" -le "$ota_slot_bytes" ] ||
  { echo "Firmware is $firmware_bytes bytes, larger than the $ota_slot_bytes-byte OTA slot." >&2; exit 1; }

if [ "$chip_family" != "ESP32-S3" ]; then
  for fatal_assert in copy_buff pkt_rxbuff; do
    ! grep -a -q "$fatal_assert" "$firmware_bin" ||
      { echo "Stock ESP-Hosted assert '$fatal_assert' found in $firmware_bin" >&2; exit 1; }
  done
  grep -a -F -q "HomeTiles RPC sync serialization active" "$firmware_bin" ||
    { echo "ESP-Hosted RPC serialization marker missing" >&2; exit 1; }
  grep -a -F -q "HomeTiles SDIO RX recovery active (a8204f9 raw PKT_LEN + pending drain)" "$firmware_bin" ||
    { echo "ESP-Hosted SDIO RX recovery marker missing" >&2; exit 1; }
  grep -a -F -q "on_full_trans_done not in IRAM" "$firmware_bin" ||
    { echo "Cache-safe DW-GDMA marker missing" >&2; exit 1; }
fi

echo "Firmware: $firmware_bin"
echo "Size: $firmware_bytes bytes"
echo "SHA256: $(sha256sum "$firmware_bin" | cut -d' ' -f1)"
