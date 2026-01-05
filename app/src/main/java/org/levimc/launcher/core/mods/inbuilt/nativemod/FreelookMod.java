package org.levimc.launcher.core.mods.inbuilt.nativemod;

public class FreelookMod {

    /**
     * Loads the native library and initializes the Freelook mod.
     * @return true if initialization was successful.
     */
    public static boolean init() {
        // Loads "libinbuiltmods.so"
        if (!InbuiltModsNative.loadLibrary()) {
            return false;
        }
        return nativeInit();
    }

    // Native methods matching the JNIEXPORTs in freelook.cpp
    public static native boolean nativeInit();
    
    // Triggers the "Cinematic Zoom Out" and Force Perspective
    public static native void nativeOnKeyDown();
    
    // Triggers the "Smooth Zoom In" and Reset Perspective
    public static native void nativeOnKeyUp();
    
    public static native boolean nativeIsActive();
}
