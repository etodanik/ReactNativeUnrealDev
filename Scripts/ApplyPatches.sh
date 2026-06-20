#!/bin/bash

if [ -z "${BASH_VERSION:-}" ]; then
  exec /bin/bash "$0" "$@"
fi

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENGINE_DIR="${1:-${SCRIPT_DIR}/../Engine}"
PATCHES_DIR="${2:-${SCRIPT_DIR}/../Patches}"

if ! command -v git &>/dev/null; then
  echo "Error: git is required but not installed."
  exit 1
fi

if [ ! -d "$ENGINE_DIR/.git" ]; then
  echo "Error: Engine git repository not found at $ENGINE_DIR"
  exit 1
fi

if [ ! -d "$PATCHES_DIR" ]; then
  echo "No Patches directory found, skipping patch application"
  exit 0
fi

if ! git -C "$ENGINE_DIR" diff --quiet --diff-filter=U --exit-code --; then
  echo "Error: Engine has unresolved conflicts. Resolve them before applying patches."
  git -C "$ENGINE_DIR" diff --name-only --diff-filter=U | sed 's/^/  /'
  exit 1
fi

shopt -s nullglob
patch_files=("$PATCHES_DIR"/*.patch)

if [ "${#patch_files[@]}" -eq 0 ]; then
  echo "No patch files found in $PATCHES_DIR"
  exit 0
fi

echo "Applying patches from $PATCHES_DIR"

print_check_output() {
  local title="$1"
  local output="$2"

  echo "$title"
  if [ -n "$output" ]; then
    printf "%s\n" "$output" | sed 's/^/  /'
  else
    echo "  (no output)"
  fi
}

for patch_file in "${patch_files[@]}"; do
  echo "Processing patch: $(basename "$patch_file")"

  reverse_check_output="$(git -C "$ENGINE_DIR" apply --reverse --check "$patch_file" 2>&1)" && reverse_check_status=0 || reverse_check_status=$?
  if [ "$reverse_check_status" -eq 0 ]; then
    echo "Patch already applied, skipping"
    continue
  fi

  apply_check_output="$(git -C "$ENGINE_DIR" apply --check "$patch_file" 2>&1)" && apply_check_status=0 || apply_check_status=$?
  if [ "$apply_check_status" -eq 0 ]; then
    git -C "$ENGINE_DIR" apply --3way "$patch_file"
    echo "Patch applied successfully"
  else
    threeway_check_output="$(git -C "$ENGINE_DIR" apply --3way --check "$patch_file" 2>&1)" && threeway_check_status=0 || threeway_check_status=$?

    echo "Patch cannot be applied by the script's plain context check: $patch_file"
    print_check_output "Already-applied check failed:" "$reverse_check_output"
    print_check_output "Plain apply check failed:" "$apply_check_output"
    if [ "$threeway_check_status" -eq 0 ]; then
      echo "3-way apply check would succeed."
      printf "To apply this patch manually, run: git -C %q apply --3way %q\n" "$ENGINE_DIR" "$patch_file"
    else
      print_check_output "3-way apply check failed:" "$threeway_check_output"
    fi
    exit 1
  fi
done

echo "All patches applied successfully"
