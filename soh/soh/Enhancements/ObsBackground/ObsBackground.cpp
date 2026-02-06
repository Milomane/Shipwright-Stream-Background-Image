#include "ObsBackground.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <SDL2/SDL.h>

#include "libultraship/libultraship.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

// For SCENE_* and gPlayState
extern "C" {
#include "global.h"
extern PlayState* gPlayState;
}

// Keep consistent with your menu macro usage
#define CVAR_OBS_BG(x) "gEnhancements.ObsBackground." x

namespace fs = std::filesystem;

namespace {
bool sInitialized = false;
int sLastScene = -1;

fs::path GetExeDirFallbackEmpty() {
    char* base = SDL_GetBasePath();
    if (base == nullptr) {
        return {};
    }
    fs::path p(base);
    SDL_free(base);
    return p;
}

fs::path GetRootDir() {
    const int useExeDir = CVarGetInteger(CVAR_ENHANCEMENT("ObsBackground.UseExeDir"), 0);
    if (useExeDir) {
        fs::path exe = GetExeDirFallbackEmpty();
        if (!exe.empty()) {
            return exe;
        }
    }

    // Writable + consistent (recommended)
    return fs::path(Ship::Context::GetInstance()->GetAppDirectoryPath());
}

fs::path GetOutDir() {
    return GetRootDir() / "obs_background";
}
fs::path GetImagesDir() {
    return GetOutDir() / "images";
}
fs::path GetCurrentPng() {
    return GetOutDir() / "current.png";
}
fs::path GetCurrentHtml() {
    return GetOutDir() / "current.html";
}

void EnsureFolders() {
    std::error_code ec;
    fs::create_directories(GetImagesDir(), ec);
}

void EnsureHtmlTemplate() {
    const fs::path htmlPath = GetCurrentHtml();
    if (fs::exists(htmlPath)) {
        return;
    }

    // Browser Source friendly: no distortion + auto-refresh
    std::ofstream f(htmlPath, std::ios::binary);
    f << R"(<!doctype html>
<html>
<head>
<meta charset="utf-8" />
<style>
  html, body { margin:0; width:100%; height:100%; overflow:hidden; background:transparent; }
  img { width:100%; height:100%; object-fit:cover; display:block; }
</style>
</head>
<body>
  <img id="img" src="current.png" />
  <script>
    const img = document.getElementById('img');
    setInterval(() => { img.src = 'current.png?cb=' + Date.now(); }, 250);
  </script>
</body>
</html>)";
}

std::vector<std::string> ListImages() {
    std::vector<std::string> out;
    std::error_code ec;
    if (!fs::exists(GetImagesDir(), ec)) {
        return out;
    }

    for (auto& e : fs::directory_iterator(GetImagesDir(), ec)) {
        if (ec || !e.is_regular_file()) {
            continue;
        }

        auto ext = e.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".webp") {
            out.push_back(e.path().filename().string());
        }
    }

    std::sort(out.begin(), out.end());
    return out;
}

// Use only scene IDs known to exist in this project (matches your existing Kokiri grouping)
bool IsKokiriOrSubarea(int sceneNum) {
    switch (sceneNum) {
        case SCENE_KOKIRI_FOREST:
        case SCENE_LINKS_HOUSE:
        case SCENE_KOKIRI_SHOP:
        case SCENE_MIDOS_HOUSE:
        case SCENE_KNOW_IT_ALL_BROS_HOUSE:
        case SCENE_TWINS_HOUSE:
        case SCENE_SARIAS_HOUSE:
            return true;
        default:
            return false;
    }
}

void CopyToCurrent(const std::string& fileName) {
    if (fileName.empty()) {
        return;
    }

    const fs::path src = GetImagesDir() / fileName;
    const fs::path dst = GetCurrentPng();

    std::error_code ec;
    if (!fs::exists(src, ec) || ec) {
        return;
    }

    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
}

void RefreshForScene(int sceneNum) {
    const bool isKokiri = IsKokiriOrSubarea(sceneNum);
    const char* cvar = isKokiri ? CVAR_OBS_BG("Image.Kokiri") : CVAR_OBS_BG("Image.Default");

    std::string pick = CVarGetString(cvar, "");
    CopyToCurrent(pick);
}

// Must match the project hook signature for OnGameFrameUpdate (no args)
void OnFrameUpdate() {
    if (!CVarGetInteger(CVAR_ENHANCEMENT("ObsBackground.Enable"), 0)) {
        return;
    }
    if (gPlayState == nullptr) {
        return;
    }

    const int scene = static_cast<int>(gPlayState->sceneNum);
    if (scene == sLastScene) {
        return;
    }

    sLastScene = scene;
    RefreshForScene(scene);
}

} // namespace

namespace ObsBackground {

void InitOnce() {
    if (sInitialized) {
        return;
    }
    sInitialized = true;

    EnsureFolders();
    EnsureHtmlTemplate();

    // Register with a compatible callback signature (project expects void())
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>(OnFrameUpdate);
}

void ForceRefresh() {
    if (gPlayState == nullptr) {
        return;
    }
    RefreshForScene(static_cast<int>(gPlayState->sceneNum));
}

void OpenOutputFolder() {
    EnsureFolders();
    EnsureHtmlTemplate();

    std::string p = fs::absolute(GetOutDir()).string();
    std::replace(p.begin(), p.end(), '\\', '/');
    SDL_OpenURL(("file:///" + p).c_str());
}

static void DrawPicker(const char* label, const char* cvarKey) {
    auto files = ListImages();
    std::string current = CVarGetString(cvarKey, "");

    if (ImGui::BeginCombo(label, current.empty() ? "<None>" : current.c_str())) {
        if (ImGui::Selectable("<None>", current.empty())) {
            CVarSetString(cvarKey, "");
        }

        for (auto& f : files) {
            const bool selected = (f == current);
            if (ImGui::Selectable(f.c_str(), selected)) {
                CVarSetString(cvarKey, f.c_str());
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }

        ImGui::EndCombo();
    }
}

void DrawPickerKokiri(WidgetInfo&) {
    DrawPicker("Kokiri Forest (+ subareas)", CVAR_OBS_BG("Image.Kokiri"));
}

void DrawPickerDefault(WidgetInfo&) {
    DrawPicker("Everywhere else", CVAR_OBS_BG("Image.Default"));
}

} // namespace ObsBackground
