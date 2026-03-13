#include "ObsBackground.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <SDL2/SDL.h>

#include "libultraship/libultraship.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

// For SCENE_* and gPlayState/gSaveContext
extern "C" {
#include "global.h"
#include "variables.h"
extern PlayState* gPlayState;
}

#define CVAR_OBS_BG(x) CVAR_ENHANCEMENT("ObsBackground.") x

namespace fs = std::filesystem;

namespace {

bool sInitialized = false;
int sLastScene = -1;

// ---------- Filesystem helpers ----------

fs::path GetExeDirFallbackEmpty() {
    char* base = SDL_GetBasePath();
    if (!base) {
        return {};
    }
    fs::path p(base);
    SDL_free(base);
    return p;
}

fs::path GetRootDir() {
    const int useExeDir = CVarGetInteger(CVAR_OBS_BG("UseExeDir"), 0);
    if (useExeDir) {
        fs::path exe = GetExeDirFallbackEmpty();
        if (!exe.empty()) {
            return exe;
        }
    }

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

bool ContainsI(std::string_view text, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }

    auto it = std::search(text.begin(), text.end(), needle.begin(), needle.end(), [](char a, char b) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(a))) ==
               static_cast<char>(std::tolower(static_cast<unsigned char>(b)));
    });

    return it != text.end();
}

void CopyToCurrent(const std::string& filename) {
    EnsureFolders();

    std::error_code ec;

    // Empty = remove current.png (lets OBS keep last frame depending on its caching, but avoids copying garbage)
    if (filename.empty()) {
        fs::remove(GetCurrentPng(), ec);
        return;
    }

    const fs::path src = GetImagesDir() / filename;
    const fs::path dst = GetCurrentPng();

    if (!fs::exists(src, ec)) {
        return;
    }

    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
}

// ---------- Area mapping ----------

enum class ObsArea : int {
    Interiors,
    Grottos,
    FairyFountains,

    Kokiri,
    LostWoods,
    SacredForestMeadow,
    HyruleField,
    LonLonRanch,
    LakeHylia,

    Market,
    TempleOfTime,
    HyruleCastle,

    KakarikoVillage,
    Graveyard,
    DeathMountainTrail,
    GoronCity,
    DeathMountainCrater,

    ZorasRiver,
    ZorasDomain,
    ZorasFountain,

    GerudoValley,
    GerudoFortress,
    Wasteland,
    DesertColossus,

    DekuTree,
    DodongosCavern,
    JabuJabu,
    ForestTemple,
    FireTemple,
    WaterTemple,
    SpiritTemple,
    ShadowTemple,
    BottomOfTheWell,
    IceCavern,
    GerudoTrainingGround,
    GanonsCastle,

    Unknown,
};

struct AreaDef {
    ObsArea id;
    const char* key;  // used for CVar: Image.<key>
    const char* name; // display name
};

static const AreaDef kAreas[] = {
    { ObsArea::Interiors, "Interiors", "Interiors (houses/shops/labs/stables)" },
    { ObsArea::Grottos, "Grottos", "Grottos" },
    { ObsArea::FairyFountains, "FairyFountains", "Fairy Fountains" },

    { ObsArea::Kokiri, "Kokiri", "Kokiri Forest" },
    { ObsArea::LostWoods, "LostWoods", "Lost Woods" },
    { ObsArea::SacredForestMeadow, "SacredForestMeadow", "Sacred Forest Meadow" },
    { ObsArea::HyruleField, "HyruleField", "Hyrule Field" },
    { ObsArea::LonLonRanch, "LonLonRanch", "Lon Lon Ranch" },
    { ObsArea::LakeHylia, "LakeHylia", "Lake Hylia" },

    { ObsArea::Market, "Market", "Market" },
    { ObsArea::TempleOfTime, "TempleOfTime", "Temple of Time" },
    { ObsArea::HyruleCastle, "HyruleCastle", "Hyrule Castle / Castle Grounds" },

    { ObsArea::KakarikoVillage, "KakarikoVillage", "Kakariko Village" },
    { ObsArea::Graveyard, "Graveyard", "Graveyard" },
    { ObsArea::DeathMountainTrail, "DeathMountainTrail", "Death Mountain Trail" },
    { ObsArea::GoronCity, "GoronCity", "Goron City" },
    { ObsArea::DeathMountainCrater, "DeathMountainCrater", "Death Mountain Crater" },

    { ObsArea::ZorasRiver, "ZorasRiver", "Zora's River" },
    { ObsArea::ZorasDomain, "ZorasDomain", "Zora's Domain" },
    { ObsArea::ZorasFountain, "ZorasFountain", "Zora's Fountain" },

    { ObsArea::GerudoValley, "GerudoValley", "Gerudo Valley" },
    { ObsArea::GerudoFortress, "GerudoFortress", "Gerudo Fortress" },
    { ObsArea::Wasteland, "Wasteland", "Haunted Wasteland" },
    { ObsArea::DesertColossus, "DesertColossus", "Desert Colossus" },

    { ObsArea::DekuTree, "DekuTree", "Deku Tree" },
    { ObsArea::DodongosCavern, "DodongosCavern", "Dodongo's Cavern" },
    { ObsArea::JabuJabu, "JabuJabu", "Jabu-Jabu's Belly" },
    { ObsArea::ForestTemple, "ForestTemple", "Forest Temple" },
    { ObsArea::FireTemple, "FireTemple", "Fire Temple" },
    { ObsArea::WaterTemple, "WaterTemple", "Water Temple" },
    { ObsArea::SpiritTemple, "SpiritTemple", "Spirit Temple" },
    { ObsArea::ShadowTemple, "ShadowTemple", "Shadow Temple" },
    { ObsArea::BottomOfTheWell, "BottomOfTheWell", "Bottom of the Well" },
    { ObsArea::IceCavern, "IceCavern", "Ice Cavern" },
    { ObsArea::GerudoTrainingGround, "GerudoTrainingGround", "Gerudo Training Ground" },
    { ObsArea::GanonsCastle, "GanonsCastle", "Ganon's Castle" },
};

const AreaDef* FindAreaDef(ObsArea id) {
    for (const auto& a : kAreas) {
        if (a.id == id) {
            return &a;
        }
    }
    return nullptr;
}

static bool IsAdultLink() {
#ifdef LINK_AGE_ADULT
    return gSaveContext.linkAge == LINK_AGE_ADULT;
#else
    // In OoT decomp, linkAge is typically 0 = child, 1 = adult.
    return gSaveContext.linkAge != 0;
#endif
}

static std::string MakeImageCVarKey(ObsArea area, bool adult) {
    const AreaDef* def = FindAreaDef(area);
    if (!def) {
        return std::string(CVAR_OBS_BG("Image.Unknown")) + (adult ? ".Adult" : ".Child");
    }
    return std::string(CVAR_OBS_BG("Image.")) + def->key + (adult ? ".Adult" : ".Child");
}

bool IsGrottoScene(uint8_t sceneNum) {
    switch (sceneNum) {
        case SCENE_GROTTOS:
        case SCENE_REDEAD_GRAVE:
        case SCENE_ROYAL_FAMILYS_TOMB:
        case SCENE_WINDMILL_AND_DAMPES_GRAVE:
            return true;
        default:
            return false;
    }
}

bool IsFairyFountainScene(uint8_t sceneNum) {
    switch (sceneNum) {
        case SCENE_GRAVE_WITH_FAIRYS_FOUNTAIN:
        case SCENE_GREAT_FAIRYS_FOUNTAIN_SPELLS:
        case SCENE_GREAT_FAIRYS_FOUNTAIN_MAGIC:
        case SCENE_FAIRYS_FOUNTAIN:
            return true;
        default:
            return false;
    }
}

bool IsInteriorScene(uint8_t sceneNum) {
    // Shops block
    if (sceneNum >= SCENE_BAZAAR && sceneNum <= SCENE_BOMBCHU_SHOP) {
        return true;
    }

    switch (sceneNum) {
        // Kokiri houses
        case SCENE_LINKS_HOUSE:
        case SCENE_KOKIRI_SHOP:
        case SCENE_MIDOS_HOUSE:
        case SCENE_SARIAS_HOUSE:
        case SCENE_TWINS_HOUSE:
        case SCENE_KNOW_IT_ALL_BROS_HOUSE:

        // Market interiors
        case SCENE_POTION_SHOP_MARKET:
        case SCENE_SHOOTING_GALLERY:
        case SCENE_BOMBCHU_BOWLING_ALLEY:
        case SCENE_TREASURE_BOX_SHOP:
        case SCENE_BACK_ALLEY_HOUSE:
        case SCENE_HAPPY_MASK_SHOP:
        case SCENE_MARKET_GUARD_HOUSE:

        // Kakariko interiors
        case SCENE_IMPAS_HOUSE:
        case SCENE_HOUSE_OF_SKULLTULA:
        case SCENE_KAKARIKO_CENTER_GUEST_HOUSE:
        case SCENE_POTION_SHOP_KAKARIKO:
        case SCENE_POTION_SHOP_GRANNY:
        case SCENE_WINDMILL_AND_DAMPES_GRAVE:

        // Other interiors
        case SCENE_GORON_SHOP:
        case SCENE_ZORA_SHOP:
        case SCENE_LON_LON_BUILDINGS:
        case SCENE_STABLE:
        case SCENE_LAKESIDE_LABORATORY:
        case SCENE_FISHING_POND:
        case SCENE_DOG_LADY_HOUSE:
        case SCENE_CARPENTERS_TENT:
        case SCENE_GRAVEKEEPERS_HUT:
            return true;

        default:
            return false;
    }
}

ObsArea GetObsAreaForScene(uint8_t sceneNum) {
    if (IsGrottoScene(sceneNum)) {
        return ObsArea::Grottos;
    }
    if (IsFairyFountainScene(sceneNum)) {
        return ObsArea::FairyFountains;
    }
    if (IsInteriorScene(sceneNum)) {
        return ObsArea::Interiors;
    }

    switch (sceneNum) {
        case SCENE_KOKIRI_FOREST:
            return ObsArea::Kokiri;
        case SCENE_LOST_WOODS:
            return ObsArea::LostWoods;
        case SCENE_SACRED_FOREST_MEADOW:
            return ObsArea::SacredForestMeadow;

        case SCENE_HYRULE_FIELD:
        case SCENE_CUTSCENE_MAP:
            return ObsArea::HyruleField;
        case SCENE_LON_LON_RANCH:
            return ObsArea::LonLonRanch;
        case SCENE_LAKE_HYLIA:
            return ObsArea::LakeHylia;

        // Market + ToT exteriors
        case SCENE_MARKET_ENTRANCE_DAY:
        case SCENE_MARKET_ENTRANCE_NIGHT:
        case SCENE_MARKET_ENTRANCE_RUINS:
        case SCENE_BACK_ALLEY_DAY:
        case SCENE_BACK_ALLEY_NIGHT:
        case SCENE_MARKET_DAY:
        case SCENE_MARKET_NIGHT:
        case SCENE_MARKET_RUINS:
        case SCENE_TEMPLE_OF_TIME_EXTERIOR_DAY:
        case SCENE_TEMPLE_OF_TIME_EXTERIOR_NIGHT:
        case SCENE_TEMPLE_OF_TIME_EXTERIOR_RUINS:
            return ObsArea::Market;

        case SCENE_TEMPLE_OF_TIME:
            return ObsArea::TempleOfTime;

        case SCENE_HYRULE_CASTLE:
        case SCENE_CASTLE_COURTYARD_GUARDS_DAY:
        case SCENE_CASTLE_COURTYARD_GUARDS_NIGHT:
        case SCENE_CASTLE_COURTYARD_ZELDA:
            return ObsArea::HyruleCastle;

        case SCENE_KAKARIKO_VILLAGE:
            return ObsArea::KakarikoVillage;
        case SCENE_GRAVEYARD:
            return ObsArea::Graveyard;

        case SCENE_DEATH_MOUNTAIN_TRAIL:
            return ObsArea::DeathMountainTrail;
        case SCENE_GORON_CITY:
            return ObsArea::GoronCity;
        case SCENE_DEATH_MOUNTAIN_CRATER:
            return ObsArea::DeathMountainCrater;

        case SCENE_ZORAS_RIVER:
            return ObsArea::ZorasRiver;
        case SCENE_ZORAS_DOMAIN:
            return ObsArea::ZorasDomain;
        case SCENE_ZORAS_FOUNTAIN:
            return ObsArea::ZorasFountain;

        case SCENE_GERUDO_VALLEY:
            return ObsArea::GerudoValley;
        case SCENE_GERUDOS_FORTRESS:
        case SCENE_THIEVES_HIDEOUT:
            return ObsArea::GerudoFortress;
        case SCENE_HAUNTED_WASTELAND:
            return ObsArea::Wasteland;
        case SCENE_DESERT_COLOSSUS:
            return ObsArea::DesertColossus;

        // Dungeons
        case SCENE_DEKU_TREE:
        case SCENE_DEKU_TREE_BOSS:
            return ObsArea::DekuTree;

        case SCENE_DODONGOS_CAVERN:
        case SCENE_DODONGOS_CAVERN_BOSS:
            return ObsArea::DodongosCavern;

        case SCENE_JABU_JABU:
        case SCENE_JABU_JABU_BOSS:
            return ObsArea::JabuJabu;

        case SCENE_FOREST_TEMPLE:
        case SCENE_FOREST_TEMPLE_BOSS:
            return ObsArea::ForestTemple;

        case SCENE_FIRE_TEMPLE:
        case SCENE_FIRE_TEMPLE_BOSS:
            return ObsArea::FireTemple;

        case SCENE_WATER_TEMPLE:
        case SCENE_WATER_TEMPLE_BOSS:
            return ObsArea::WaterTemple;

        case SCENE_SPIRIT_TEMPLE:
        case SCENE_SPIRIT_TEMPLE_BOSS:
            return ObsArea::SpiritTemple;

        case SCENE_SHADOW_TEMPLE:
        case SCENE_SHADOW_TEMPLE_BOSS:
            return ObsArea::ShadowTemple;

        case SCENE_BOTTOM_OF_THE_WELL:
            return ObsArea::BottomOfTheWell;
        case SCENE_ICE_CAVERN:
            return ObsArea::IceCavern;
        case SCENE_GERUDO_TRAINING_GROUND:
            return ObsArea::GerudoTrainingGround;

        // Ganon
        case SCENE_INSIDE_GANONS_CASTLE:
        case SCENE_GANONS_TOWER:
        case SCENE_GANONS_TOWER_COLLAPSE_INTERIOR:
        case SCENE_INSIDE_GANONS_CASTLE_COLLAPSE:
        case SCENE_GANONS_TOWER_COLLAPSE_EXTERIOR:
        case SCENE_OUTSIDE_GANONS_CASTLE:
        case SCENE_GANONDORF_BOSS:
        case SCENE_GANON_BOSS:
            return ObsArea::GanonsCastle;

        default:
            return ObsArea::Unknown;
    }
}

// ---------- UI helpers ----------

static void DrawPickerWithList(const char* label, const char* cvarKey, const std::vector<std::string>& files) {
    std::string current = CVarGetString(cvarKey, "");

    if (ImGui::BeginCombo(label, current.empty() ? "<None>" : current.c_str())) {
        if (ImGui::Selectable("<None>", current.empty())) {
            CVarSetString(cvarKey, "");
        }
        for (auto& f : files) {
            bool selected = (f == current);
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

void RefreshForScene(uint8_t sceneNum) {
    const ObsArea area = GetObsAreaForScene(sceneNum);
    const bool adult = !IsAdultLink();

    auto WithSuffix = [](const char* base, bool isAdult) {
        return std::string(base) + (isAdult ? ".Adult" : ".Child");
    };

    std::string pick;

    // 1) Area-specific (try current age, then the other age)
    if (area == ObsArea::Unknown) {
        const std::string keyPrimary   = WithSuffix(CVAR_OBS_BG("Image.Unknown"), adult);
        const std::string keySecondary = WithSuffix(CVAR_OBS_BG("Image.Unknown"), !adult);

        pick = CVarGetString(keyPrimary.c_str(), "");
        if (pick.empty()) pick = CVarGetString(keySecondary.c_str(), "");

        // Legacy single-slot key
        if (pick.empty()) pick = CVarGetString(CVAR_OBS_BG("Image.Unknown"), "");
    } else {
        const std::string keyPrimary   = MakeImageCVarKey(area, adult);
        const std::string keySecondary = MakeImageCVarKey(area, !adult);

        pick = CVarGetString(keyPrimary.c_str(), "");
        if (pick.empty()) pick = CVarGetString(keySecondary.c_str(), "");

        // Legacy single-slot key
        if (pick.empty()) {
            if (const AreaDef* def = FindAreaDef(area)) {
                const std::string legacyKey = std::string(CVAR_OBS_BG("Image.")) + def->key;
                pick = CVarGetString(legacyKey.c_str(), "");
            }
        }
    }

    // 2) Default fallback (per-age, then legacy)
    if (pick.empty()) {
        const std::string defPrimary   = WithSuffix(CVAR_OBS_BG("Image.Default"), adult);
        const std::string defSecondary = WithSuffix(CVAR_OBS_BG("Image.Default"), !adult);

        pick = CVarGetString(defPrimary.c_str(), "");
        if (pick.empty()) pick = CVarGetString(defSecondary.c_str(), "");
        if (pick.empty()) pick = CVarGetString(CVAR_OBS_BG("Image.Default"), "");
    }

    CopyToCurrent(pick);
}

void OnFrameUpdate() {
    if (!CVarGetInteger(CVAR_OBS_BG("Enable"), 0)) {
        return;
    }
    if (!gPlayState) {
        return;
    }

    const int scene = static_cast<int>(gPlayState->sceneNum);
    if (scene == sLastScene) {
        return;
    }

    sLastScene = scene;
    RefreshForScene(static_cast<uint8_t>(scene));
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

    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>(OnFrameUpdate);
}

void ForceRefresh() {
    if (!gPlayState) {
        return;
    }

    RefreshForScene(gPlayState->sceneNum);
}

void OpenOutputFolder() {
    EnsureFolders();
    EnsureHtmlTemplate();

    std::string p = fs::absolute(GetOutDir()).string();
    std::replace(p.begin(), p.end(), '\\', '/');
    SDL_OpenURL(("file:///" + p).c_str());
}

void DrawPickerDefault(WidgetInfo&) {
    const auto files = ListImages();

    const std::string keyChild = std::string(CVAR_OBS_BG("Image.Default")) + ".Child";
    const std::string keyAdult = std::string(CVAR_OBS_BG("Image.Default")) + ".Adult";

    ImGui::TextUnformatted("Default fallback:");
    ImGui::Indent();

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Child");
    ImGui::SameLine(120.0f);
    ImGui::SetNextItemWidth(260.0f);
    DrawPickerWithList("##default_child", keyChild.c_str(), files);

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Adult");
    ImGui::SameLine(120.0f);
    ImGui::SetNextItemWidth(260.0f);
    DrawPickerWithList("##default_adult", keyAdult.c_str(), files);

    ImGui::Unindent();
}

void DrawAreaPickerList(WidgetInfo&) {
    EnsureFolders();
    EnsureHtmlTemplate();

    const auto files = ListImages();

    static char filter[96] = { 0 };
    ImGui::InputText("Search", filter, sizeof(filter));

    ImGui::Spacing();
    ImGui::TextUnformatted("Fallbacks:");

    // Unknown per-age
    {
        const std::string keyChild = std::string(CVAR_OBS_BG("Image.Unknown")) + ".Child";
        const std::string keyAdult = std::string(CVAR_OBS_BG("Image.Unknown")) + ".Adult";

        ImGui::Indent();
        ImGui::TextUnformatted("Unknown scene:");
        ImGui::SameLine(160.0f);
        ImGui::TextDisabled("(if scene not mapped)");

        ImGui::Indent();
        ImGui::TextUnformatted("Child");
        ImGui::SameLine(120.0f);
        ImGui::SetNextItemWidth(260.0f);
        DrawPickerWithList("##unknown_child", keyChild.c_str(), files);

        ImGui::TextUnformatted("Adult");
        ImGui::SameLine(120.0f);
        ImGui::SetNextItemWidth(260.0f);
        DrawPickerWithList("##unknown_adult", keyAdult.c_str(), files);
        ImGui::Unindent();
        ImGui::Unindent();
    }

    // Default per-age
    {
        const std::string keyChild = std::string(CVAR_OBS_BG("Image.Default")) + ".Child";
        const std::string keyAdult = std::string(CVAR_OBS_BG("Image.Default")) + ".Adult";

        ImGui::Indent();
        ImGui::TextUnformatted("Default:");
        ImGui::SameLine(160.0f);
        ImGui::TextDisabled("(if area empty)");

        ImGui::Indent();
        ImGui::TextUnformatted("Child");
        ImGui::SameLine(120.0f);
        ImGui::SetNextItemWidth(260.0f);
        DrawPickerWithList("##fallback_default_child", keyChild.c_str(), files);

        ImGui::TextUnformatted("Adult");
        ImGui::SameLine(120.0f);
        ImGui::SetNextItemWidth(260.0f);
        DrawPickerWithList("##fallback_default_adult", keyAdult.c_str(), files);
        ImGui::Unindent();
        ImGui::Unindent();
    }

    ImGui::Separator();

    // Main table: Area | Child | Adult
    if (ImGui::BeginTable("##obs_bg_table", 3,
                          ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
                              ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Area", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Child", ImGuiTableColumnFlags_WidthFixed, 280.0f);
        ImGui::TableSetupColumn("Adult", ImGuiTableColumnFlags_WidthFixed, 280.0f);
        ImGui::TableHeadersRow();

        for (const auto& a : kAreas) {
            if (!ContainsI(a.name, filter) && !ContainsI(a.key, filter)) {
                continue;
            }

            const std::string cvarChild = std::string(CVAR_OBS_BG("Image.")) + a.key + ".Child";
            const std::string cvarAdult = std::string(CVAR_OBS_BG("Image.")) + a.key + ".Adult";

            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(a.name);

            ImGui::TableSetColumnIndex(1);
            ImGui::PushID((std::string(a.key) + "_child").c_str());
            ImGui::SetNextItemWidth(260.0f);
            DrawPickerWithList("##child", cvarChild.c_str(), files);
            ImGui::PopID();

            ImGui::TableSetColumnIndex(2);
            ImGui::PushID((std::string(a.key) + "_adult").c_str());
            ImGui::SetNextItemWidth(260.0f);
            DrawPickerWithList("##adult", cvarAdult.c_str(), files);
            ImGui::PopID();
        }

        ImGui::EndTable();
    }
}

void AutoAssignMissing() {
    // Stub: you mentioned this exists in your current system.
    // Keeping it here so the build/link doesn't break if the menu references it.
}

} // namespace ObsBackground
