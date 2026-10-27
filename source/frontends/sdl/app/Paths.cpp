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
    if (std::filesystem::exists(dest / ".extracted")) {
        return;
    }
    std::filesystem::create_directories(dest, ec);
    fprintf(stderr, "Paths: extracting to %s\\n", dest.string().c_str());

    JNIEnv* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity) {
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

    std::function<void(const std::string&, const std::filesystem::path&)> extract = [&](const std::string& assetPath, const std::filesystem::path& destPath) {
        AAssetDir* dir = AAssetManager_openDir(mgr, assetPath.c_str());
        if (!dir) {
            AAsset* asset = AAssetManager_open(mgr, assetPath.c_str(), AASSET_MODE_STREAMING);
            if (!asset) {
                return;
            }
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
        std::filesystem::create_directories(destPath, ec);
        const char* name = nullptr;
        while ((name = AAssetDir_getNextFileName(dir)) != nullptr) {
            std::string childAsset = assetPath.empty() ? std::string(name) : assetPath + "/" + name;
            std::filesystem::path childDest = destPath / name;
            extract(childAsset, childDest);
        }
        AAssetDir_close(dir);
    };

    extract("fonts", dest / "fonts");
    extract("levels", dest / "levels");

    std::ofstream marker(dest / ".extracted");
    marker << "1";

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
