package com.example.targetspeed;

import org.libsdl.app.SDLActivity;

/** Thin wrapper: SDLActivity loads libSDL2.so + libmain.so and runs SDL_main(). */
public class MainActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }
}
