package org.levimc.launcher.core.mods.inbuilt.overlay;

import android.app.Activity;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.widget.ImageButton;

import org.levimc.launcher.R;
import org.levimc.launcher.core.mods.inbuilt.model.ModIds;
// Ensure you have created this class, or comment out references to it until you do:
import org.levimc.launcher.core.mods.inbuilt.nativemod.FreelookMod; 

public class FreelookOverlay extends BaseOverlayButton {
    private static final String TAG = "FreelookOverlay";
    private boolean isActive = false;
    private boolean initialized = false;
    private final Handler handler = new Handler(Looper.getMainLooper());

    public FreelookOverlay(Activity activity) {
        super(activity);
    }

    @Override
    protected String getModId() {
        return ModIds.FREELOOK;
    }

    @Override
    protected int getIconResource() {
        // Ensure this icon exists in your drawable folder, or use R.drawable.ic_menu_view as a temporary placeholder
        return R.drawable.ic_menu_view; 
    }

    @Override
    public void show(int startX, int startY) {
        if (!initialized) {
            initializeNative();
        }
        super.show(startX, startY);
    }

    public void initializeForKeyboard() {
        if (!initialized) {
            initializeNative();
        }
    }

    private void initializeNative() {
        handler.postDelayed(() -> {
            // Checks if the native mod is ready
            if (FreelookMod.init()) {
                initialized = true;
                Log.i(TAG, "Freelook native initialized successfully");
            } else {
                Log.e(TAG, "Failed to initialize freelook native");
            }
        }, 1000);
    }

    @Override
    protected void onButtonClick() {
        if (!initialized) {
            Log.w(TAG, "Freelook not initialized yet");
            return;
        }

        isActive = !isActive;

        if (isActive) {
            FreelookMod.nativeOnKeyDown();
            updateButtonState(true);
        } else {
            FreelookMod.nativeOnKeyUp();
            updateButtonState(false);
        }
    }

    public void onKeyDown() {
        if (!initialized) {
            Log.w(TAG, "Freelook not initialized yet");
            return;
        }
        if (isActive) return;

        isActive = true;
        FreelookMod.nativeOnKeyDown();
        updateButtonState(true);
    }

    public void onKeyUp() {
        if (!initialized || !isActive) return;

        isActive = false;
        FreelookMod.nativeOnKeyUp();
        updateButtonState(false);
    }

    private void updateButtonState(boolean active) {
        if (overlayView instanceof ImageButton) {
            ImageButton btn = (ImageButton) overlayView;
            float userOpacity = getButtonOpacity();
            btn.setAlpha(userOpacity);
            // Assumes you have these background drawables (from Snaplook example)
            btn.setBackgroundResource(active ? R.drawable.bg_overlay_button_active : R.drawable.bg_overlay_button);
        }
    }

    @Override
    public void hide() {
        if (isActive && initialized) {
            FreelookMod.nativeOnKeyUp();
            isActive = false;
        }
        super.hide();
    }

    public boolean isActive() {
        return isActive;
    }

    public boolean isInitialized() {
        return initialized;
    }
                     }
    
