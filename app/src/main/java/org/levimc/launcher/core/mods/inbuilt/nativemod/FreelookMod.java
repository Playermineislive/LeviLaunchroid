package org.levimc.launcher.core.mods.inbuilt.nativemod;

public class FreelookMod {
    
    /**
     * Initializes the native freelook hook.
     * @return true if initialization was successful.
     */
    public static native boolean init();

    /**
     * Called when the freelook button is pressed (or toggled on).
     * Triggers the camera unlock in the native game code.
     */
    public static native void nativeOnKeyDown();

    /**
     * Called when the freelook button is released (or toggled off).
     * Resets the camera to the player's view in the native game code.
     */
    public static native void nativeOnKeyUp();
}
