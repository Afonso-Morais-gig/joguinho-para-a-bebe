#!/usr/bin/env bash
# Fetches SDL2 and copies its Java glue (SDLActivity etc.) into the Android project.
set -euo pipefail

SDL_TAG="${SDL_TAG:-release-2.30.9}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDL_DIR="$ROOT/third_party/SDL2"
JAVA_DST="$ROOT/android/app/src/main/java/org/libsdl/app"

if [ ! -f "$SDL_DIR/CMakeLists.txt" ]; then   # skip clone if SDL2 is already present (e.g. from the zip)
    git clone --depth 1 --branch "$SDL_TAG" https://github.com/libsdl-org/SDL.git "$SDL_DIR"
fi

mkdir -p "$JAVA_DST"
cp "$SDL_DIR"/android-project/app/src/main/java/org/libsdl/app/*.java "$JAVA_DST/"

echo "SDL2 ready ($SDL_TAG)."
echo "Next: open ./android in Android Studio, or run: cd android && gradle wrapper && ./gradlew assembleDebug"
