
#!/bin/bash

# Resolve the absolute path of the script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Check if git exists
if ! command -v git &>/dev/null; then
  echo "Error: git is required but not installed."
  exit 1
fi

# Target Engine directory
ENGINE_DIR="${SCRIPT_DIR}/../Engine"
PATCHES_DIR="${SCRIPT_DIR}/../Patches"

# Check if Engine folder exists
if [ ! -d "$ENGINE_DIR" ]; then
  echo "Engine folder not found. Cloning..."
  git clone --branch 5.7.4-release --single-branch --depth 1 git@github.com:EpicGames/UnrealEngine.git "$ENGINE_DIR"
else
  echo "Engine folder exists. Pulling latest changes..."
  cd "$ENGINE_DIR" || exit 1
  git reset --hard HEAD
  git pull
fi

if [ -d "$PATCHES_DIR" ]; then
  echo "Applying patches..."
  cd "$ENGINE_DIR" || exit 1
  
  for patch_file in "$PATCHES_DIR"/*.patch; do
    if [ -f "$patch_file" ]; then
      echo "Processing patch: $(basename "$patch_file")"
      
      if git apply --reverse --check "$patch_file" 2>/dev/null; then
        echo "Patch already applied, skipping"
      elif git apply --check "$patch_file" 2>/dev/null; then
        git apply --3way "$patch_file"
        echo "Patch applied successfully"
      else
        echo "Patch cannot be applied (conflicts or other issues)"
        exit 1
      fi
    fi
  done
  
  echo "All patches applied successfully"
else
  echo "No Patches directory found, skipping patch application"
fi