# Acerta o 6 7 (Android, C + SDL2)

Tap the targets before the orange ring runs out. 3 lives, and targets get faster as your score climbs.

## Layout

- `src/main.c` – the whole game (same code runs on desktop and Android)
- `android/` – Gradle project; `MainActivity` extends SDL's `SDLActivity`
- `android/app/jni/CMakeLists.txt` – builds SDL2 + `libmain.so` with the NDK
- `scripts/setup_android.sh` – clones SDL2 into `third_party/` and copies its Java glue
- `Makefile` – desktop build for quick testing (`make run`)

## Build the APK

Requirements: Android Studio (or SDK + NDK + CMake 3.22 via sdkmanager), JDK 17, Gradle.

```
./scripts/setup_android.sh
cd android
gradle wrapper          # once, generates ./gradlew
./gradlew assembleDebug
```

APK: `android/app/build/outputs/apk/debug/app-debug.apk`
Install: `adb install -r android/app/build/outputs/apk/debug/app-debug.apk`

Or just open the `android/` folder in Android Studio and press Run.
If your NDK version differs, edit `ndkVersion` in `android/app/build.gradle`.
Create `android/local.properties` with `sdk.dir=/path/to/Android/Sdk` if Gradle can't find the SDK.

## Desktop testing

```
make run
```
(needs `libsdl2-dev`)

## Tweaks

- `MISS_TAP_COSTS_LIFE` in `main.c`: set to 1 if tapping empty space should also cost a life.
- Difficulty curve lives in `SpawnTarget()`.
- Change package name/app id in `android/app/build.gradle` and move `MainActivity.java` to match.

## Build automático (GitHub Actions)

Não precisas de instalar nada localmente. Faz push deste projeto para um repositório no GitHub:
o workflow `.github/workflows/android.yml` instala o SDK/NDK, descarrega o SDL2 e gera o APK.
Depois de concluído, vai a **Actions → Build Android APK → Artifacts** e descarrega `target-speed-debug-apk`.
Também podes correr manualmente com **Run workflow**.
