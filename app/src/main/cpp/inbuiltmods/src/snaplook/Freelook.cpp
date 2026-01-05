#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <cstring>
#include <cstdint>
#include <sys/mman.h>
#include <unistd.h>
#include <fstream>
#include <string>

// Assuming these exist in your project based on zoom.txt
#include "common/transition.h"
#include "pl/Gloss.h"

#define LOG_TAG "LeviFreelook"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// --- Global State ---
static bool g_initialized = false;
static bool g_freelookActive = false;
static Transition g_fovTransition;
static uint64_t g_defaultFov = 0;

// Function pointers to originals
static int (*g_orig_getPerspective)(void*) = nullptr;
static uint64_t (*g_orig_getFOV)(void*) = nullptr;

// --- Helper: Memory Scanning ---
// Finds the base address of a loaded library (e.g., libminecraftpe.so)
static uintptr_t GetLibraryBase(const char* libName) {
    std::ifstream maps("/proc/self/maps");
    std::string line;
    while (std::getline(maps, line)) {
        if (line.find(libName) != std::string::npos && line.find("r-xp") != std::string::npos) {
            uintptr_t start, end;
            if (sscanf(line.c_str(), "%lx-%lx", &start, &end) == 2) {
                return start;
            }
        }
    }
    return 0;
}

// Scans for a VTable address by its TypeInfo name
static uintptr_t FindVTable(const char* libName, const char* typeInfoName) {
    uintptr_t libBase = GetLibraryBase(libName);
    if (libBase == 0) return 0;

    size_t nameLen = strlen(typeInfoName);
    uintptr_t typeInfoNameAddr = 0;
    std::string line;

    // 1. Find the RTTI Name string
    std::ifstream maps("/proc/self/maps");
    while (std::getline(maps, line)) {
        if (line.find(libName) == std::string::npos) continue;
        if (line.find("r--p") == std::string::npos && line.find("r-xp") == std::string::npos) continue;

        uintptr_t start, end;
        if (sscanf(line.c_str(), "%lx-%lx", &start, &end) != 2) continue;

        for (uintptr_t addr = start; addr < end - nameLen; addr++) {
            if (memcmp((void*)addr, typeInfoName, nameLen) == 0) {
                typeInfoNameAddr = addr;
                break;
            }
        }
        if (typeInfoNameAddr != 0) break;
    }

    if (typeInfoNameAddr == 0) return 0;

    // 2. Find the TypeInfo Pointer
    uintptr_t typeInfoAddr = 0;
    std::ifstream maps2("/proc/self/maps");
    while (std::getline(maps2, line)) {
        if (line.find(libName) == std::string::npos) continue;
        if (line.find("r--p") == std::string::npos) continue;

        uintptr_t start, end;
        if (sscanf(line.c_str(), "%lx-%lx", &start, &end) != 2) continue;

        for (uintptr_t addr = start; addr < end - sizeof(void*); addr += sizeof(void*)) {
            if (*(uintptr_t*)addr == typeInfoNameAddr) {
                typeInfoAddr = addr - sizeof(void*);
                break;
            }
        }
        if (typeInfoAddr != 0) break;
    }

    if (typeInfoAddr == 0) return 0;

    // 3. Find the VTable pointing to TypeInfo
    std::ifstream maps3("/proc/self/maps");
    while (std::getline(maps3, line)) {
        if (line.find(libName) == std::string::npos) continue;
        if (line.find("r--p") == std::string::npos) continue;

        uintptr_t start, end;
        if (sscanf(line.c_str(), "%lx-%lx", &start, &end) != 2) continue;

        for (uintptr_t addr = start; addr < end - sizeof(void*); addr += sizeof(void*)) {
            if (*(uintptr_t*)addr == typeInfoAddr) {
                // VTable start is usually found here, + sizeof(void*) to skip the TypeInfo ptr
                return addr + sizeof(void*);
            }
        }
    }
    return 0;
}

// Helper to hook a specific VTable index safely
static bool HookVTableIndex(uintptr_t vtableAddr, int index, void* hookFunc, void** origFuncStore) {
    if (vtableAddr == 0) return false;

    uintptr_t* slot = (uintptr_t*)(vtableAddr + index * sizeof(void*));
    
    // Page alignment for mprotect
    uintptr_t pageStart = (uintptr_t)slot & ~(4095UL);
    
    // Store original
    if (origFuncStore) {
        *origFuncStore = (void*)(*slot);
    }

    // Unprotect memory
    if (mprotect((void*)pageStart, 4096, PROT_READ | PROT_WRITE) != 0) {
        return false;
    }

    // Write hook
    *slot = (uintptr_t)hookFunc;

    // Reprotect memory
    mprotect((void*)pageStart, 4096, PROT_READ);
    
    return true;
}

// --- Hook Functions ---

// 1. Perspective Hook (VanillaCameraAPI)
static int Hook_getPlayerViewPerspectiveOption(void* thisPtr) {
    if (g_freelookActive) {
        return 1; // Force "Third Person Back"
    }
    if (g_orig_getPerspective) {
        return g_orig_getPerspective(thisPtr);
    }
    return 0;
}

// 2. FOV Hook (CameraAPI)
static uint64_t Hook_tryGetFOV(void* thisPtr) {
    if (!g_orig_getFOV) return 0;

    uint64_t currentFov = g_orig_getFOV(thisPtr);
    
    // Always update default FOV when not animating/modding, to keep it fresh
    if (!g_fovTransition.inProgress() && !g_freelookActive) {
        g_defaultFov = currentFov;
    }
    // Fallback if it was never set (Dry Run Fix 1)
    if (g_defaultFov == 0) g_defaultFov = currentFov;

    // Handle Animation
    if (g_fovTransition.inProgress()) {
        g_fovTransition.tick();
        return g_fovTransition.getCurrent();
    }

    // Handle Static Freelook State (Zoomed out slightly)
    if (g_freelookActive) {
        // Adding roughly 15% to FOV value for "Cinematic" feel
        // Based on ZoomMod, higher value = wider view/zoom out
        return g_defaultFov + 8000000ULL; 
    }

    return currentFov;
}

static bool InstallHooks() {
    // Hook 1: VanillaCameraAPI (Perspective)
    uintptr_t vanillaVTable = FindVTable("libminecraftpe.so", "16VanillaCameraAPI");
    if (vanillaVTable == 0) {
        LOGE("Failed to find VanillaCameraAPI vtable");
        return false;
    }
    
    if (!HookVTableIndex(vanillaVTable, 7, (void*)Hook_getPlayerViewPerspectiveOption, (void**)&g_orig_getPerspective)) {
        LOGE("Failed to hook Perspective");
        return false;
    }

    // Hook 2: CameraAPI (FOV)
    uintptr_t cameraVTable = FindVTable("libminecraftpe.so", "9CameraAPI");
    if (cameraVTable == 0) {
        LOGE("Failed to find CameraAPI vtable");
        return false;
    }

    if (!HookVTableIndex(cameraVTable, 7, (void*)Hook_tryGetFOV, (void**)&g_orig_getFOV)) {
        LOGE("Failed to hook FOV");
        return false;
    }

    return true;
}

extern "C" {

JNIEXPORT jboolean JNICALL
Java_org_levimc_launcher_core_mods_inbuilt_nativemod_FreelookMod_nativeInit(JNIEnv* env, jclass clazz) {
    if (g_initialized) return JNI_TRUE;

    LOGI("Initializing Freelook Mod...");
    GlossInit(true);

    if (!InstallHooks()) {
        LOGE("Freelook initialization failed");
        return JNI_FALSE;
    }

    g_initialized = true;
    LOGI("Freelook initialized successfully");
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_org_levimc_launcher_core_mods_inbuilt_nativemod_FreelookMod_nativeOnKeyDown(JNIEnv* env, jclass clazz) {
    if (!g_initialized || g_freelookActive) return;

    // Safety check: Don't start if we haven't read FOV yet (Dry Run Fix 1)
    if (g_defaultFov == 0) {
        g_freelookActive = true; // Just enable flag, skip anim
        return;
    }

    g_freelookActive = true;
    
    // Smoothly widen FOV
    // From: Default -> To: Default + 8,000,000
    g_fovTransition.startTransition(g_defaultFov, g_defaultFov + 8000000ULL, 150);
}

JNIEXPORT void JNICALL
Java_org_levimc_launcher_core_mods_inbuilt_nativemod_FreelookMod_nativeOnKeyUp(JNIEnv* env, jclass clazz) {
    if (!g_initialized || !g_freelookActive) return;

    g_freelookActive = false;

    if (g_defaultFov != 0) {
        // Smoothly return FOV
        // From: Default + 8,000,000 -> To: Default
        g_fovTransition.startTransition(g_defaultFov + 8000000ULL, g_defaultFov, 150);
    }
}

JNIEXPORT jboolean JNICALL
Java_org_levimc_launcher_core_mods_inbuilt_nativemod_FreelookMod_nativeIsActive(JNIEnv* env, jclass clazz) {
    return g_freelookActive ? JNI_TRUE : JNI_FALSE;
}

}
