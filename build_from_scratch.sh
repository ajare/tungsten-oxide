#!/usr/bin/env bash
set -Eeuo pipefail

usage() {
  cat <<'EOF'
Usage: ./build_from_scratch.sh [options]

Build TungstenOxide, Willpower, MassivePolyPusher, and all required submodules
from clean Linux build trees.

Options:
  --with-mpp-lfs       Download MassivePolyPusher's Git LFS files.
  --config CONFIG      Build configuration (default: Release).
  --build-dir DIR      TungstenOxide build directory, relative to this repository
                       unless absolute (default: build-linux).
  -h, --help           Show this help.

Environment:
  CC, CXX               Select the C and C++ compilers during configuration.
  CMAKE_GENERATOR       Select a CMake generator.
  CMAKE_BUILD_PARALLEL_LEVEL
                        Limit the number of parallel build jobs.
EOF
}

fail() { printf 'error: %s\n' "$*" >&2; exit 1; }
WITH_MPP_LFS=false
BUILD_TYPE=Release
BUILD_DIR=build-linux
while (($#)); do
  case "$1" in
    --with-mpp-lfs) WITH_MPP_LFS=true; shift ;;
    --config) (($# >= 2)) || fail "--config requires a value"; BUILD_TYPE=$2; shift 2 ;;
    --build-dir) (($# >= 2)) || fail "--build-dir requires a value"; BUILD_DIR=$2; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) fail "unknown option: $1 (run with --help for usage)" ;;
  esac
done

command -v git >/dev/null || fail "Git is required but was not found on PATH"
command -v cmake >/dev/null || fail "CMake is required but was not found on PATH"
ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
cd "$ROOT_DIR"
git rev-parse --is-inside-work-tree >/dev/null 2>&1 || fail "$ROOT_DIR is not a Git checkout"
[[ -f CMakeLists.txt && -f .gitmodules ]] || fail "run this script from the TungstenOxide checkout"

printf 'Synchronizing and checking out all submodules...\n'
git submodule sync --recursive
git submodule update --init --recursive
SUBMODULE_STATUS=$(git submodule status --recursive)
printf '%s\n' "$SUBMODULE_STATUS"
grep -Eq '^[+-U]' <<<"$SUBMODULE_STATUS" && fail "one or more submodules are not at their recorded commits"

WILLPOWER_DIR="$ROOT_DIR/ext/willpower"
MPP_DIR="$WILLPOWER_DIR/ext/massive-poly-pusher"
[[ -f "$WILLPOWER_DIR/build_from_scratch.sh" ]] || fail "Willpower was not checked out correctly"
[[ -f "$ROOT_DIR/ext/yaml-cpp/CMakeLists.txt" ]] || fail "yaml-cpp was not checked out correctly"
[[ -f "$ROOT_DIR/ext/nativefiledialog-extended/CMakeLists.txt" ]] || fail "Native File Dialog Extended was not checked out correctly"
[[ -f "$ROOT_DIR/ext/googletest/CMakeLists.txt" ]] || fail "GoogleTest was not checked out correctly"

willpower_args=(--build-type "$BUILD_TYPE" --build-dir build)
[[ "$WITH_MPP_LFS" == true ]] && willpower_args+=(--with-mpp-lfs)
printf 'Building Willpower and MassivePolyPusher from scratch...\n'
bash "$WILLPOWER_DIR/build_from_scratch.sh" "${willpower_args[@]}"

# Linux application modules link MPP's static support libraries into shared objects, so the
# standalone dependency tree must compile those archives as position-independent code.
MPP_BUILD_DIR="$WILLPOWER_DIR/build/_deps/massive-poly-pusher-build"
cmake -S "$MPP_DIR" -B "$MPP_BUILD_DIR" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DCMAKE_POSITION_INDEPENDENT_CODE=ON

# The root project also consumes MPP components that Willpower itself does not.
printf 'Building supplemental MassivePolyPusher targets...\n'
cmake --build "$MPP_BUILD_DIR" --config "$BUILD_TYPE" --parallel \
  --target MppResourceParsers MppAppSupport assimp

[[ "$BUILD_DIR" = /* ]] || BUILD_DIR="$ROOT_DIR/$BUILD_DIR"
[[ -n "$BUILD_DIR" && "$BUILD_DIR" != / && "$BUILD_DIR" != "$ROOT_DIR" ]] || fail "refusing to remove unsafe build directory: $BUILD_DIR"
case "$ROOT_DIR/" in "$BUILD_DIR/"*) fail "refusing to remove a directory containing the checkout: $BUILD_DIR" ;; esac
case "$BUILD_DIR/" in "$WILLPOWER_DIR/"*) fail "build directory must not be inside ext/willpower" ;; esac

printf 'Removing previous TungstenOxide build output...\n'
rm -rf -- "$BUILD_DIR"
printf 'Configuring TungstenOxide %s build in %s...\n' "$BUILD_TYPE" "$BUILD_DIR"
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
printf 'Building TungstenOxide...\n'
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel

printf 'Build completed successfully.\n'
printf 'Launcher:       %s/src/launcher/Launcher\n' "$BUILD_DIR"
printf 'Track editor:   %s/src/editor/track_editor\n' "$BUILD_DIR"
printf 'Model tool:     %s/src/model-tool/model_tool\n' "$BUILD_DIR"
printf 'glTF converter: %s/src/gltf-convert/gltf_convert\n' "$BUILD_DIR"
