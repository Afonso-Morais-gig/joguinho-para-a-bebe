#!/usr/bin/env bash
# Makes sure SDL2 sources are present and the Java glue is in place. Safe to run repeatedly.
set -euo pipefail

SDL_TAG="${SDL_TAG:-release-2.30.9}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDL_DIR="$ROOT/third_party/SDL2"
JAVA_DST="$ROOT/android/app/src/main/java/org/libsdl/app"
JAVA_SRC="$SDL_DIR/android-project/app/src/main/java/org/libsdl/app"

if [ ! -f "$SDL_DIR/CMakeLists.txt" ]; then
    git clone --depth 1 --branch "$SDL_TAG" https://github.com/libsdl-org/SDL.git "$SDL_DIR"
fi

# The Java glue is already committed under android/. Only refresh it if the SDL source provides it.
if [ -d "$JAVA_SRC" ]; then
    mkdir -p "$JAVA_DST"
    cp "$JAVA_SRC"/*.java "$JAVA_DST/"
fi

echo "SDL2 ready ($SDL_TAG)."
