#!/usr/bin/env bash
set -euo pipefail

repo_root="$(dirname "${BASH_SOURCE[0]}")"
cd "$repo_root"
src_root="$PWD/.ossfuzz-src"
work_root="$PWD/.ossfuzz"
out_root="$PWD/.ossfuzz/out"

ensure_dep() {
  local name="$1"
  local url="$2"
  local target="$src_root/$name"

  if [[ -e "$target" ]]; then
    return 0
  fi
  if [[ "${ALLOW_NETWORK:-}" != "1" ]]; then
    echo "Missing dependency: $name at $target"
    echo "Set ALLOW_NETWORK=1 to let this script clone it from:"
    echo "  $url"
    exit 1
  fi
  git clone "$url" "$target"
}

mkdir -p "$src_root"
ln -sfn "$PWD" "$src_root/fwupd"

ensure_dep "libcbor" "https://github.com/PJK/libcbor"
ensure_dep "glib" "https://gitlab.gnome.org/GNOME/glib.git"
ensure_dep "libxmlb" "https://github.com/hughsie/libxmlb"

# Clean stale dependency builds if headers are missing.
if [[ ! -d "$work_root/include/glib-2.0" ]] && [[ -d "$src_root/glib/.ossfuzz" ]]; then
  rm -rf "$src_root/glib/.ossfuzz"
fi
if [[ ! -d "$work_root/include/libxmlb-2" ]] && [[ -d "$src_root/libxmlb/.ossfuzz" ]]; then
  rm -rf "$src_root/libxmlb/.ossfuzz"
fi
if [[ ! -d "$work_root/include/cbor" ]] && [[ -d "$src_root/libcbor/.ossfuzz" ]]; then
  rm -rf "$src_root/libcbor/.ossfuzz"
fi

export CC="${CC:-clang}"
export CXX="${CXX:-clang++}"
export CFLAGS="${CFLAGS:--fsanitize=address -fno-omit-frame-pointer -Wno-implicit-function-declaration}"
export CXXFLAGS="${CXXFLAGS:--fsanitize=address -fno-omit-frame-pointer}"
export LDFLAGS="${LDFLAGS:--fsanitize=address}"
export SRC="$src_root"
export WORK="$work_root"
export OUT="$out_root"

python3 "contrib/ci/oss-fuzz.py"
./run_oob_repro.sh
