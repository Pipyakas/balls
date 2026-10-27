#include "app/Paths.h"

#include <SDL3/SDL_filesystem.h>
#if defined(__ANDROID__)
#include <SDL3/SDL_system.h>
#include <SDL3/SDL.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <jni.h>
#include <fstream>
#include <cstdio>
#include <functional>
#endif

namespace {

std::filesystem::path executableDirectory() {
    const char* base = SDL_GetBasePath();
    return base ? std::filesystem::path(reinterpret_cast<const char8_t*>(base)) : std::filesystem::current_path();
}

}

#if defined(__ANDROID__)
static void extractAndroidAssets(const std::filesystem::path& dest) {
    std::error_code ec;
    const std::filesystem::path marker = dest / ".extracted";
    constexpr const char* extractionVersion = "4";
    // An older build created directories in place of files (AAssetManager_openDir succeeds on a
    // file path), so an unpacks with any other marker is thrown away and redone.
    if (std::filesystem::exists(marker)) {
        std::ifstream previous(marker);
        std::string version;
        previous >> version;
        if (version == extractionVersion) {
            return;
        }
        std::filesystem::remove_all(dest, ec);
    }
    std::filesystem::create_directories(dest, ec);
    SDL_Log("Paths: extracting assets to %s", dest.string().c_str());

    JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) {
        SDL_Log("Paths: no JNI env (%p) or activity (%p)", static_cast<void*>(env), static_cast<void*>(activity));
        return;
    }
    jclass activityClass = env->GetObjectClass(activity);
    jmethodID getAssets = env->GetMethodID(activityClass, "getAssets", "()Landroid/content/res/AssetManager;");
    if (!getAssets) {
        env->DeleteLocalRef(activityClass);
        return;
    }
    jobject assetManagerObj = env->CallObjectMethod(activity, getAssets);
    AAssetManager* mgr = AAssetManager_fromJava(env, assetManagerObj);
    SDL_Log("Paths: asset manager %p", static_cast<void*>(mgr));

    // AAssetDir_getNextFileName lists files only, never subdirectories, so the level folders
    // looked empty. AssetManager.list() returns files and directories alike.
    jclass assetManagerClass = env->GetObjectClass(assetManagerObj);
    jmethodID listAssets = env->GetMethodID(assetManagerClass, "list", "(Ljava/lang/String;)[Ljava/lang/String;");

    std::function<void(const std::string&, const std::filesystem::path&)> extract =
        [&](const std::string& assetPath, const std::filesystem::path& destPath) {
            AAsset* asset = AAssetManager_open(mgr, assetPath.c_str(), AASSET_MODE_STREAMING);
            if (asset) {
                std::filesystem::create_directories(destPath.parent_path(), ec);
                std::ofstream out(destPath, std::ios::binary | std::ios::trunc);
                char buffer[65536];
                int bytesRead = 0;
                while ((bytesRead = AAsset_read(asset, buffer, sizeof(buffer))) > 0) {
                    out.write(buffer, bytesRead);
                }
                AAsset_close(asset);
                return;
            }
            if (!listAssets) {
                return;
            }
            jstring directoryPath = env->NewStringUTF(assetPath.c_str());
            auto names = static_cast<jobjectArray>(env->CallObjectMethod(assetManagerObj, listAssets, directoryPath));
            env->DeleteLocalRef(directoryPath);
            if (!names) {
                SDL_Log("Paths: cannot list %s", assetPath.c_str());
                return;
            }
            std::filesystem::create_directories(destPath, ec);
            const jsize count = env->GetArrayLength(names);
            for (jsize index = 0; index < count; ++index) {
                auto nameValue = static_cast<jstring>(env->GetObjectArrayElement(names, index));
                const char* name = env->GetStringUTFChars(nameValue, nullptr);
                const std::string childAsset = assetPath.empty() ? std::string(name) : assetPath + "/" + name;
                extract(childAsset, destPath / name);
                env->ReleaseStringUTFChars(nameValue, name);
                env->DeleteLocalRef(nameValue);
            }
            env->DeleteLocalRef(names);
            SDL_Log("Paths: %s has %d entries", assetPath.c_str(), static_cast<int>(count));
        };

    extract("fonts", dest / "fonts");
    extract("levels", dest / "levels");

    std::ofstream written(marker);
    written << extractionVersion;

    if (assetManagerClass) {
        env->DeleteLocalRef(assetManagerClass);
    }
    if (assetManagerObj) {
        env->DeleteLocalRef(assetManagerObj);
    }
    env->DeleteLocalRef(activityClass);
}
#endif

std::filesystem::path assetRoot() {
    static const std::filesystem::path root = [] {
#if defined(__ANDROID__)
        const char* internal = SDL_GetAndroidInternalStoragePath();
        std::filesystem::path base = internal ? std::filesystem::path(internal) : std::filesystem::current_path();
        std::filesystem::path dest = base / "assets";
        extractAndroidAssets(dest);
        return dest;
#elif defined(BALLS_DEV_ASSET_DIR)
        if (std::filesystem::exists(std::filesystem::path(BALLS_DEV_ASSET_DIR) / "levels")) {
            return std::filesystem::path(BALLS_DEV_ASSET_DIR);
        }
        // A dev build whose source tree moved falls back to whatever sits beside the executable.
        return std::filesystem::current_path() / "assets";
#else
        for (auto directory = executableDirectory(); !directory.empty(); directory = directory.parent_path()) {
            if (std::filesystem::exists(directory / "assets" / "levels")) {
                return directory / "assets";
            }
            if (directory == directory.parent_path()) {
                break;
            }
        }
        return std::filesystem::current_path() / "assets";
#endif
    }();
    return root;
}

std::filesystem::path levelDirectory(GameMode mode) {
    return assetRoot() / "levels" / (mode == GameMode::Normal ? "normal" : "performance");
}

std::filesystem::path fontPath(std::string_view fileName) { return assetRoot() / "fonts" / fileName; }

std::filesystem::path pathFromUtf8(const char* text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text)));
}

std::string utf8Of(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}
