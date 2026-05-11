#!/bin/bash

# Resolve the absolute path of the script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Check if git exists
if ! command -v git &>/dev/null; then
  echo "Error: git is required but not installed."
  exit 1
fi

# Target Plugin directory
PLUGIN_DIR="${SCRIPT_DIR}/../Plugins/React"

# Check if Plugin folder exists
if [ ! -d "$PLUGIN_DIR" ]; then
  echo "Plugin folder not found. Cloning..."
  git clone --branch main --single-branch --depth 1 git@github.com:etodanik/ReactNativeUnreal.git "$PLUGIN_DIR"
else
  echo "Plugin folder exists. Pulling latest changes..."
  cd "$PLUGIN_DIR" || exit 1
  git pull
fi