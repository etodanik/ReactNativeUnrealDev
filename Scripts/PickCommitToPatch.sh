#!/bin/bash

if [ -z "${BASH_VERSION:-}" ]; then
  exec /bin/bash "$0" "$@"
fi

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
ENGINE_DIR="${REPO_ROOT}/Engine"
PATCHES_DIR="${REPO_ROOT}/Patches"

if ! command -v git &>/dev/null; then
  echo "Error: git is required but not installed."
  exit 1
fi

if ! command -v awk &>/dev/null; then
  echo "Error: awk is required but not installed."
  exit 1
fi

if [ ! -d "${ENGINE_DIR}/.git" ]; then
  echo "Error: Engine git repository not found at ${ENGINE_DIR}"
  exit 1
fi

if [ "$#" -eq 0 ]; then
  echo "Usage: $0 <commit> [commit ...]"
  exit 1
fi

mkdir -p "${PATCHES_DIR}"

make_patch() {
  local commit="$1"
  local full_commit
  local commit_timestamp
  local output

  full_commit="$(git -C "${ENGINE_DIR}" rev-parse "${commit}^{commit}")"
  commit_timestamp="$(TZ=UTC git -C "${ENGINE_DIR}" show -s --format=%cd --date=format-local:%Y%m%dT%H%M%SZ "${full_commit}")"
  output="${PATCHES_DIR}/${commit_timestamp}-${full_commit}.patch"

  echo "Writing ${output}"

  git -C "${ENGINE_DIR}" format-patch -1 --stdout "${full_commit}" | awk -v upstream="${full_commit}" '
    /^---$/ && !inserted {
      print ""
      print "(cherry picked from commit " upstream ")"
      inserted = 1
    }
    { print }
  ' > "${output}"

  GENERATED_PATCHES+=("${output}")
}

GENERATED_PATCHES=()
for commit in "$@"; do
  make_patch "${commit}"
done

echo "Patch series written to ${PATCHES_DIR}"
echo "Apply generated patches in timestamp order with:"
printf "git -C %q am --3way" "${ENGINE_DIR}"
printf "%s\n" "${GENERATED_PATCHES[@]}" | LC_ALL=C sort | while IFS= read -r patch; do
  printf " %q" "${patch}"
done
printf "\n"
