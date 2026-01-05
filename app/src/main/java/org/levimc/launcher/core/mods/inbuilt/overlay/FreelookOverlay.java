package org.levimc.launcher.core.mods.inbuilt.overlay;

import android.app.Activity;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.widget.ImageButton;

import org.levimc.launcher.R;
import org.levimc.launcher.core.mods.inbuilt.model.ModIds;
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
        // Ensure you have an icon named 'ic_freelook' in drawable folder
        // If not, use R.drawable.ic_snaplook as a placeholder
        return R.drawable.ic_snaplook; 
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
        // Delay initialization slightly to ensure library is ready
        handler.postDelayed(() -> {
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
        toggleFreelook();
    }

    // Call this if using a keyboard/physical button
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

    private void toggleFreelook() {
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

    private void updateButtonState(boolean active) {
        if (overlayView instanceof ImageButton) {
            ImageButton btn = (ImageButton) overlayView;
            float userOpacity = getButtonOpacity();
            btn.setAlpha(userOpacity);
            btn.setBackgroundResource(active ? R.drawable.bg_overlay_button_active : R.drawable.bg_overlay_button);
        }
    }

    @Override
    public void hide() {
        // Turn off freelook if the button is hidden
        if (isActive && initialized) {
            FreelookMod.nativeOnKeyUp();
            isActive = false;
        }
        super.hide();
    }

    public boolean isActive() {
        return isActive;
    }
                  }
          
