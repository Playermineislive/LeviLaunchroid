#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <cstring>
#include <cstdint>
#include <sys/mman.h>
#include <unistd.h>
#include <fstream>
#include <string>
#include <atomic>

// Headers assumed to exist in your project structure (common in this launcher base)
#include "common/transition.h"
#include "pl/Gloss.h"

#define LOG_TAG "LeviFreelook"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// --- Global State (Thread Safe) ---
static std::atomic<bool> g_initialized(false);
static std::atomic<bool> g_freelookActive(false);

// Transition state (Managed on Game Thread)
static Transition g_fovTransition;
static uint64_t g_defaultFov = 0;
static bool g_shouldStartTransition = false; // Flag to sync start
static bool g_shouldEndTransition = false;   // Flag to sync end

// Function pointers to originals
static int (*g_orig_getPerspective)(void*) = nullptr;
static uint64_t (*g_orig_getFOV)(void*) = nullptr;

// --- Helper: Memory Scanning (Static to avoid linker conflicts) ---
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

static uintptr_t FindVTable(const char* libName, const char* typeInfoName) {
    uintptr_t libBase = GetLibraryBase(libName);
    if (libBase == 0) return 0;

    size_t nameLen = strlen(typeInfoName);
    uintptr_t typeInfoNameAddr = 0;
    std::string line;

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

    // Find pointer to TypeInfo
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

    // Find VTable
    std::ifstream maps3("/proc/self/maps");
    while (std::getline(maps3, line)) {
        if (line.find(libName) == std::string::npos) continue;
        if (line.find("r--p") == std::string::npos) continue;
        uintptr_t start, end;
        if (sscanf(line.c_str(), "%lx-%lx", &start, &end) != 2) continue;
        for (uintptr_t addr = start; addr < end - sizeof(void*); addr += sizeof(void*)) {
            if (*(uintptr_t*)addr == typeInfoAddr) {
                return addr + sizeof(void*);
            }
        }
    }
    return 0;
}

static bool HookVTableIndex(uintptr_t vtableAddr, int index, void* hookFunc, void** origFuncStore) {
    if (vtableAddr == 0) return false;
    uintptr_t* slot = (uintptr_t*)(vtableAddr + index * sizeof(void*));
    uintptr_t pageStart = (uintptr_t)slot & ~(4095UL);
    if (origFuncStore) *origFuncStore = (void*)(*slot);
    if (mprotect((void*)pageStart, 4096, PROT_READ | PROT_WRITE) != 0) return false;
    *slot = (uintptr_t)hookFunc;
    mprotect((void*)pageStart, 4096, PROT_READ);
    return true;
}

// --- Hook Logic ---

// Hook 1: VanillaCameraAPI::getPlayerViewPerspectiveOption
// Returns: 0=First Person, 1=Third Person Back, 2=Third Person Front
static int Hook_getPlayerViewPerspectiveOption(void* thisPtr) {
    // If Freelook is active, force "Third Person Back" mode
    if (g_freelookActive.load()) {
        return 1; 
    }
    if (g_orig_getPerspective) {
        return g_orig_getPerspective(thisPtr);
    }
    return 0;
}

// Hook 2: CameraAPI::tryGetFOV
static uint64_t Hook_tryGetFOV(void* thisPtr) {
    if (!g_orig_getFOV) return 0;

    uint64_t currentFov = g_orig_getFOV(thisPtr);
    
    // Capture default FOV when mod is idle
    if (!g_fovTransition.inProgress() && !g_freelookActive.load()) {
        g_defaultFov = currentFov;
    }
    if (g_defaultFov == 0) g_defaultFov = currentFov;

    // --- Sync Logic (Game Thread) ---
    // Start transition request from UI thread
    if (g_shouldStartTransition) {
        g_fovTransition.startTransition(g_defaultFov, g_defaultFov + 8000000ULL, 150);
        g_shouldStartTransition = false;
    }
    // End transition request from UI thread
    if (g_shouldEndTransition) {
        g_fovTransition.startTransition(g_defaultFov + 8000000ULL, g_defaultFov, 150);
        g_shouldEndTransition = false;
    }

    // Process Animation
    if (g_fovTransition.inProgress()) {
        g_fovTransition.tick();
        return g_fovTransition.getCurrent();
    }

    // Static State
    if (g_freelookActive.load()) {
        return g_defaultFov + 8000000ULL; 
    }

    return currentFov;
}

static bool InstallHooks() {
    // 16VanillaCameraAPI -> Index 7 (getPlayerViewPerspectiveOption)
    uintptr_t vanillaVTable = FindVTable("libminecraftpe.so", "16VanillaCameraAPI");
    if (vanillaVTable != 0) {
        HookVTableIndex(vanillaVTable, 7, (void*)Hook_getPlayerViewPerspectiveOption, (void**)&g_orig_getPerspective);
    } else {
        LOGE("Failed to find VanillaCameraAPI");
        return false;
    }

    // 9CameraAPI -> Index 7 (tryGetFOV)
    uintptr_t cameraVTable = FindVTable("libminecraftpe.so", "9CameraAPI");
    if (cameraVTable != 0) {
        HookVTableIndex(cameraVTable, 7, (void*)Hook_tryGetFOV, (void**)&g_orig_getFOV);
    } else {
        LOGE("Failed to find CameraAPI");
        return false;
    }

    return true;
}

// --- JNI Exports (Synced with FreelookMod.java) ---

extern "C" {

// Java: public static native boolean init();
JNIEXPORT jboolean JNICALL
Java_org_levimc_launcher_core_mods_inbuilt_nativemod_FreelookMod_init(JNIEnv* env, jclass clazz) {
    if (g_initialized.load()) return JNI_TRUE;

    LOGI("Initializing Freelook Mod...");
    GlossInit(true); // Helper init

    if (!InstallHooks()) {
        LOGE("Freelook Hooks Failed!");
        return JNI_FALSE;
    }

    g_initialized.store(true);
    LOGI("Freelook Initialized.");
    return JNI_TRUE;
}

// Java: public static native void nativeOnKeyDown();
JNIEXPORT void JNICALL
Java_org_levimc_launcher_core_mods_inbuilt_nativemod_FreelookMod_nativeOnKeyDown(JNIEnv* env, jclass clazz) {
    if (!g_initialized.load() || g_freelookActive.load()) return;

    LOGI("Freelook ON");
    g_freelookActive.store(true);
    
    // Request animation start on next game frame
    g_shouldStartTransition = true;
    g_shouldEndTransition = false; 
}

// Java: public static native void nativeOnKeyUp();
JNIEXPORT void JNICALL
Java_org_levimc_launcher_core_mods_inbuilt_nativemod_FreelookMod_nativeOnKeyUp(JNIEnv* env, jclass clazz) {
    if (!g_initialized.load() || !g_freelookActive.load()) return;

    LOGI("Freelook OFF");
    g_freelookActive.store(false);

    // Request animation end on next game frame
    g_shouldStartTransition = false;
    g_shouldEndTransition = true;
}

} // extern "C"
