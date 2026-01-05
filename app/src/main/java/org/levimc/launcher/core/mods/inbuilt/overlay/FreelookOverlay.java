package org.levimc.launcher.core.mods.inbuilt.overlay;

import android.app.Activity;
import android.graphics.Color;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.ImageView;

import org.levimc.launcher.R;
import org.levimc.launcher.core.mods.inbuilt.manager.InbuiltModManager;
import org.levimc.launcher.core.mods.inbuilt.model.ModIds;

public class FreelookOverlay {
    private final Activity activity;
    private ImageView overlayButton;
    private boolean isVisible = false;
    private final InbuiltModManager modManager;

    public FreelookOverlay(Activity activity) {
        this.activity = activity;
        this.modManager = InbuiltModManager.getInstance(activity);
        init();
    }

    private void init() {
        overlayButton = new ImageView(activity);
        // You can use a generic icon like 'ic_menu_view' or create an eye icon drawable
        // If R.drawable.ic_eye doesn't exist, change this to R.drawable.ic_menu_search or similar
        overlayButton.setImageResource(android.R.drawable.ic_menu_view); 
        
        overlayButton.setBackgroundColor(Color.parseColor("#4D000000")); // Semi-transparent black
        overlayButton.setPadding(20, 20, 20, 20);

        FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(
            dpToPx(modManager.getOverlayButtonSize(ModIds.FREELOOK)), 
            dpToPx(modManager.getOverlayButtonSize(ModIds.FREELOOK))
        );
        
        params.gravity = Gravity.TOP | Gravity.LEFT;
        params.leftMargin = dpToPx(modManager.getOverlayPositionX(ModIds.FREELOOK, 100));
        params.topMargin = dpToPx(modManager.getOverlayPositionY(ModIds.FREELOOK, 200));
        
        overlayButton.setLayoutParams(params);

        // Touch listener to handle dragging (to move the button) or clicking
        overlayButton.setOnTouchListener(new View.OnTouchListener() {
            private int lastAction;
            private int initialX, initialY;
            private float initialTouchX, initialTouchY;

            @Override
            public boolean onTouch(View v, MotionEvent event) {
                switch (event.getAction()) {
                    case MotionEvent.ACTION_DOWN:
                        lastAction = MotionEvent.ACTION_DOWN;
                        initialX = params.leftMargin;
                        initialY = params.topMargin;
                        initialTouchX = event.getRawX();
                        initialTouchY = event.getRawY();
                        return true;
                    case MotionEvent.ACTION_UP:
                        if (lastAction == MotionEvent.ACTION_DOWN) {
                            // Handle Click: Trigger Freelook logic here
                            // You might need to call a method in your game bridge to start looking around
                            // Example: GameBridge.sendKey(KeyCodes.FREELOOK);
                        }
                        // Save new position
                        modManager.setOverlayPosition(ModIds.FREELOOK, pxToDp(params.leftMargin), pxToDp(params.topMargin));
                        return true;
                    case MotionEvent.ACTION_MOVE:
                        int dx = (int) (event.getRawX() - initialTouchX);
                        int dy = (int) (event.getRawY() - initialTouchY);
                        
                        params.leftMargin = initialX + dx;
                        params.topMargin = initialY + dy;
                        overlayButton.setLayoutParams(params);
                        lastAction = MotionEvent.ACTION_MOVE;
                        return true;
                }
                return false;
            }
        });
    }

    public void show() {
        if (!isVisible && activity.getWindow() != null) {
            ViewGroup decorView = (ViewGroup) activity.getWindow().getDecorView();
            decorView.addView(overlayButton);
            isVisible = true;
        }
    }

    public void hide() {
        if (isVisible && activity.getWindow() != null) {
            ViewGroup decorView = (ViewGroup) activity.getWindow().getDecorView();
            decorView.removeView(overlayButton);
            isVisible = false;
        }
    }

    public void update() {
        if (overlayButton != null) {
            FrameLayout.LayoutParams params = (FrameLayout.LayoutParams) overlayButton.getLayoutParams();
            params.width = dpToPx(modManager.getOverlayButtonSize(ModIds.FREELOOK));
            params.height = dpToPx(modManager.getOverlayButtonSize(ModIds.FREELOOK));
            int opacity = modManager.getOverlayOpacity(ModIds.FREELOOK);
            overlayButton.setAlpha(opacity / 100f);
            overlayButton.setLayoutParams(params);
        }
    }

    private int dpToPx(int dp) {
        float density = activity.getResources().getDisplayMetrics().density;
        return Math.round(dp * density);
    }
    
    private int pxToDp(int px) {
        float density = activity.getResources().getDisplayMetrics().density;
        return Math.round(px / density);
    }
          }
