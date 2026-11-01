// Turbo Power
//
// Turbo: statt einer festen Speedhack-Geschwindigkeit rechnet die Mod pro Bild so viele Spielschritte, wie in eine
// feste Rechenzeit passen (hoechstens N-fach). Leere Szenen laufen dadurch sehr schnell, Szenen mit vielen Objekten
// automatisch langsamer, statt dass sich das Spiel aufschaukelt und einfriert. Die Schrittgroesse passt sich laufend an
// die gemessene Rechenzeit an.
//
// Windows: solange der Turbo laeuft, wird GD nicht gedrosselt (kein "Effizienzmodus"), bekommt eine hoehere Prioritaet
// und Windows schaltet auf den Energiesparplan "Hoechstleistung" (bzw. Energiemodus "Beste Leistung"). 20 Sekunden
// nach dem letzten Turbo-Bild, beim Beenden und nach einem Absturz beim naechsten Start wird alles zurueckgestellt.

#include <Geode/Geode.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/Notification.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

using namespace geode::prelude;

namespace {

// Markierungen, die der Demon Bot (tony.demon-bot) an das Level haengt
constexpr char const* BOT_OVERLAY = "tony.demon-bot/overlay";            // Bot spielt dieses Level
constexpr char const* BOT_SEARCH = "tony.demon-bot/platformer-search";   // Platformer-Bot sucht gerade (rechnet selbst)

constexpr double HOLD_SEC = 20.0;          // so lange bleibt volle Leistung nach dem letzten Turbo-Bild an
// Jedes Update kostet GD eine feste Grundzeit (Sichtbarkeit, Kamera, Effekte) plus Zeit pro Physik-Tick. Grosse Stuecke
// sparen Grundzeit, kleine treffen die Rechenzeit genauer. Simuliert: 2 Stuecke pro Bild, 1/30 s bis 0,5 s Spielzeit.
constexpr double MIN_CHUNK = 1.0 / 30.0;   // kleinstes Stueck Spielzeit pro Update (8 Physik-Ticks)
constexpr double MAX_CHUNK = 0.5;          // groesstes Stueck (120 Physik-Ticks)
constexpr double PIECES = 2.0;             // die Rechenzeit eines Bildes wird in etwa so viele Stuecke geteilt

double nowSec() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

bool flag(std::string_view key) {
    return Mod::get()->getSettingValue<bool>(key);
}

int64_t number(std::string_view key) {
    return Mod::get()->getSettingValue<int64_t>(key);
}

// =====================================================================
// Windows: volle Leistung
// =====================================================================
#ifdef GEODE_IS_WINDOWS
namespace win {

// ---- Prozess: keine Energiespar-Drosselung (EcoQoS), hoehere Prioritaet ----
bool cpuBoosted = false;
DWORD prevClass = 0;
int prevThread = THREAD_PRIORITY_ERROR_RETURN;

void cpu(bool on) {
    if (on == cpuBoosted) return;
    PROCESS_POWER_THROTTLING_STATE ps{};
    ps.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    ps.ControlMask = on ? PROCESS_POWER_THROTTLING_EXECUTION_SPEED : 0;   // 0 = Windows entscheidet wieder selbst
    ps.StateMask = 0;                                                      // gesetzt + 0 = nie drosseln
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &ps, sizeof(ps));
    THREAD_POWER_THROTTLING_STATE ts{};
    ts.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
    ts.ControlMask = on ? THREAD_POWER_THROTTLING_EXECUTION_SPEED : 0;
    ts.StateMask = 0;
    SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &ts, sizeof(ts));
    if (on) {
        // nur anheben, nie eine von Hand gesetzte hoehere Prioritaet absenken
        prevClass = GetPriorityClass(GetCurrentProcess());
        if (prevClass == NORMAL_PRIORITY_CLASS || prevClass == BELOW_NORMAL_PRIORITY_CLASS ||
            prevClass == IDLE_PRIORITY_CLASS) {
            SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
        } else {
            prevClass = 0;
        }
        prevThread = GetThreadPriority(GetCurrentThread());
        if (prevThread != THREAD_PRIORITY_ERROR_RETURN && prevThread < THREAD_PRIORITY_ABOVE_NORMAL) {
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
        } else {
            prevThread = THREAD_PRIORITY_ERROR_RETURN;
        }
    } else {
        if (prevClass) SetPriorityClass(GetCurrentProcess(), prevClass);
        if (prevThread != THREAD_PRIORITY_ERROR_RETURN) SetThreadPriority(GetCurrentThread(), prevThread);
        prevClass = 0;
        prevThread = THREAD_PRIORITY_ERROR_RETURN;
    }
    cpuBoosted = on;
    log::info("Windows-Leistung: {}", on ? "voll (keine Drosselung, hoehere Prioritaet)" : "normal");
}

// ---- Energiesparplan "Hoechstleistung" bzw. Energiemodus "Beste Leistung" ----
constexpr GUID PLAN_HIGH = {0x8c5e7fda, 0xe8bf, 0x4a96, {0x9a, 0x85, 0xa6, 0xe2, 0x3a, 0x8c, 0x63, 0x5c}};
constexpr GUID PLAN_ULTIMATE = {0xe9a42b02, 0xd5df, 0x448d, {0xaa, 0x00, 0x03, 0xf1, 0x47, 0x49, 0xeb, 0x61}};
constexpr GUID OVERLAY_BEST = {0xded574b5, 0x45a0, 0x4f42, {0x87, 0x37, 0x46, 0x34, 0x5c, 0x09, 0xc2, 0x38}};

using GetSchemeFn = DWORD(WINAPI*)(HKEY, GUID**);
using SetSchemeFn = DWORD(WINAPI*)(HKEY, GUID const*);
using GetOverlayFn = DWORD(WINAPI*)(GUID*);
using SetOverlayFn = DWORD(WINAPI*)(GUID);   // GUID als Wert (x64: Zeiger auf eine Kopie)

// powrprof.dll erst zur Laufzeit laden: keine zusaetzliche Bibliothek zum Linken, fehlende Funktionen = nullptr
struct PowrProf {
    GetSchemeFn getScheme = nullptr;
    SetSchemeFn setScheme = nullptr;
    GetOverlayFn getOverlay = nullptr;
    SetOverlayFn setOverlay = nullptr;

    PowrProf() {
        HMODULE dll = LoadLibraryW(L"powrprof.dll");
        if (!dll) return;
        getScheme = reinterpret_cast<GetSchemeFn>(GetProcAddress(dll, "PowerGetActiveScheme"));
        setScheme = reinterpret_cast<SetSchemeFn>(GetProcAddress(dll, "PowerSetActiveScheme"));
        getOverlay = reinterpret_cast<GetOverlayFn>(GetProcAddress(dll, "PowerGetActualOverlayScheme"));
        setOverlay = reinterpret_cast<SetOverlayFn>(GetProcAddress(dll, "PowerSetActiveOverlayScheme"));
    }
};

PowrProf& powrprof() {
    static PowrProf p;
    return p;
}

bool same(GUID const& a, GUID const& b) {
    return std::memcmp(&a, &b, sizeof(GUID)) == 0;
}

std::string toStr(GUID const& g) {
    return fmt::format("{:08x}-{:04x}-{:04x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
                       static_cast<uint32_t>(g.Data1), static_cast<unsigned>(g.Data2), static_cast<unsigned>(g.Data3),
                       static_cast<unsigned>(g.Data4[0]), static_cast<unsigned>(g.Data4[1]),
                       static_cast<unsigned>(g.Data4[2]), static_cast<unsigned>(g.Data4[3]),
                       static_cast<unsigned>(g.Data4[4]), static_cast<unsigned>(g.Data4[5]),
                       static_cast<unsigned>(g.Data4[6]), static_cast<unsigned>(g.Data4[7]));
}

bool fromStr(std::string const& s, GUID& g) {
    unsigned d1 = 0, d2 = 0, d3 = 0, b[8] = {};
    if (std::sscanf(s.c_str(), "%8x-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x", &d1, &d2, &d3, &b[0], &b[1], &b[2], &b[3],
                    &b[4], &b[5], &b[6], &b[7]) != 11) {
        return false;
    }
    g.Data1 = d1;
    g.Data2 = static_cast<unsigned short>(d2);
    g.Data3 = static_cast<unsigned short>(d3);
    for (int i = 0; i < 8; i++) g.Data4[i] = static_cast<unsigned char>(b[i]);
    return true;
}

// Alten Stand dauerhaft merken, damit er auch nach einem Absturz beim naechsten Start zurueckkommt
void remember(char const* kind, GUID const& prev) {
    Mod::get()->setSavedValue<std::string>("power-kind", kind);
    Mod::get()->setSavedValue<std::string>("power-prev", toStr(prev));
    (void)Mod::get()->saveData();
}

void planUp() {
    auto& p = powrprof();
    if (p.getScheme && p.setScheme) {
        GUID* cur = nullptr;
        if (p.getScheme(nullptr, &cur) == ERROR_SUCCESS && cur) {
            GUID prev = *cur;
            LocalFree(cur);
            if (same(prev, PLAN_HIGH) || same(prev, PLAN_ULTIMATE)) {
                log::info("Energiesparplan ist schon auf voller Leistung");
                return;
            }
            if (p.setScheme(nullptr, &PLAN_HIGH) == ERROR_SUCCESS) {
                remember("plan", prev);
                log::info("Energiesparplan: Hoechstleistung (vorher {})", toStr(prev));
                return;
            }
        }
    }
    // Plan "Hoechstleistung" gibt es nicht (z. B. Modern Standby): Energiemodus "Beste Leistung" (Windows 10/11)
    if (p.getOverlay && p.setOverlay) {
        GUID prev{};
        if (p.getOverlay(&prev) == ERROR_SUCCESS) {
            if (same(prev, OVERLAY_BEST)) {
                log::info("Energiemodus ist schon 'Beste Leistung'");
                return;
            }
            if (p.setOverlay(OVERLAY_BEST) == ERROR_SUCCESS) {
                remember("overlay", prev);
                log::info("Energiemodus: Beste Leistung (vorher {})", toStr(prev));
                return;
            }
        }
    }
    log::warn("Energiesparplan konnte nicht umgestellt werden");
}

// persist = sofort auf die Platte schreiben (beim Speichern des Spiels passiert das danach ohnehin)
void planRestore(bool persist) {
    auto kind = Mod::get()->getSavedValue<std::string>("power-kind", "");
    if (kind.empty()) return;
    auto& p = powrprof();
    GUID prev{};
    if (fromStr(Mod::get()->getSavedValue<std::string>("power-prev", ""), prev)) {
        // nur zurueckstellen, wenn inzwischen niemand selbst umgestellt hat
        if (kind == "plan" && p.getScheme && p.setScheme) {
            GUID* cur = nullptr;
            if (p.getScheme(nullptr, &cur) == ERROR_SUCCESS && cur) {
                bool ours = same(*cur, PLAN_HIGH);
                LocalFree(cur);
                if (ours) p.setScheme(nullptr, &prev);
            }
        } else if (kind == "overlay" && p.getOverlay && p.setOverlay) {
            GUID cur{};
            if (p.getOverlay(&cur) == ERROR_SUCCESS && same(cur, OVERLAY_BEST)) p.setOverlay(prev);
        }
        log::info("Energiesparplan zurueckgestellt ({})", toStr(prev));
    }
    Mod::get()->setSavedValue<std::string>("power-kind", std::string());
    Mod::get()->setSavedValue<std::string>("power-prev", std::string());
    if (persist) (void)Mod::get()->saveData();
}

}  // namespace win
#endif

// =====================================================================
// Volle Leistung an, solange der Turbo laeuft (plus HOLD_SEC, damit es zwischen zwei Leveln nicht flackert)
// =====================================================================
struct Power {
    static inline bool active = false;
    static inline double lastUse = 0;

    static void set(bool on, bool persist) {
        if (on == active) return;
#ifdef GEODE_IS_WINDOWS
        if (on) {
            if (flag("windows-boost")) win::cpu(true);
            if (flag("power-plan")) win::planUp();
        } else {
            win::cpu(false);
            win::planRestore(persist);
        }
#else
        (void)persist;
#endif
        active = on;
    }

    static void use() {
        lastUse = nowSec();
        set(true, true);
    }

    static void tick() {
        if (active && nowSec() - lastUse > HOLD_SEC) set(false, true);
    }

    // Einstellung geaendert: mit den neuen Werten neu anwenden
    static void reapply() {
        if (!active) return;
        set(false, true);
        set(true, true);
    }
};

// =====================================================================
// Turbo
// =====================================================================
struct Turbo {
    enum class State { Off, PowerOnly, Run };

    static inline int depth = 0;            // Verschachtelung von GJBaseGameLayer::update (nur die aeusserste zaehlt)
    static inline double lastFrame = 0;     // Echtzeit des letzten Turbo-Bildes
    static inline double cost = 0.01;       // gemessene Rechenzeit (s) pro Spielsekunde
    static inline double shown = 0;         // angezeigtes Tempo
    static inline double tLabel = 0;
    static inline bool running = false;
    static inline bool muted = false;
    static inline bool botStopped = false;  // Demon Bot wurde in diesem Level angehalten (ESC)

    static State state(GJBaseGameLayer* gl) {
        if (!flag("enabled")) return State::Off;
        auto pl = PlayLayer::get();
        if (!pl || static_cast<GJBaseGameLayer*>(pl) != gl) return State::Off;
        if (pl->getChildByID(BOT_SEARCH)) return State::PowerOnly;   // Platformer-Bot teilt sich die Zeit selbst ein
        if (flag("only-demon-bot") && (botStopped || !pl->getChildByID(BOT_OVERLAY))) return State::Off;
        if (pl->m_isPaused || pl->m_hasCompletedLevel || pl->m_levelEndAnimationStarted) return State::Off;
        return State::Run;
    }

    static bool keepGoing(PlayLayer* pl) {
        return PlayLayer::get() == pl && !pl->m_isPaused && !pl->m_hasCompletedLevel &&
               !pl->m_levelEndAnimationStarted && !pl->getChildByID(BOT_SEARCH);
    }

    static void mute(bool on) {
        if (on == muted) return;
        auto fm = FMODAudioEngine::sharedEngine();
        FMOD::ChannelGroup* master = nullptr;
        if (fm && fm->m_system && fm->m_system->getMasterChannelGroup(&master) == FMOD_OK && master) {
            master->setMute(on);   // nur zur Laufzeit: die Lautstaerke-Einstellungen von GD bleiben unberuehrt
        }
        muted = on;
    }

    static void frame(GJBaseGameLayer* gl) {
        auto pl = static_cast<PlayLayer*>(gl);
        double t0 = nowSec();
        // Echtzeit seit dem letzten Turbo-Bild (ein Speedhack wird dabei ignoriert)
        double real = lastFrame > 0 ? std::clamp(t0 - lastFrame, 0.0, 0.1) : 1.0 / 60.0;
        double target = static_cast<double>(number("max-speed")) * real;   // gewuenschte Spielzeit fuer dieses Bild
        if (target < 1.0 / 240.0) return;   // sehr hohe Bildrate: die Zeit im naechsten Bild mitnehmen
        lastFrame = t0;
        double budget = static_cast<double>(number("frame-ms")) / 1000.0;
        double done = 0;
        if (flag("mute")) mute(true);
        Power::use();
        running = true;
        while (target - done > 1.0 / 480.0) {
            // ein Stueck soll etwa 1/PIECES der Rechenzeit brauchen
            double plan = std::clamp(budget / (PIECES * cost), MIN_CHUNK, MAX_CHUNK);
            double chunk = std::min(plan, target - done);
            double s = nowSec();
            gl->update(static_cast<float>(chunk));
            double took = nowSec() - s;
            done += chunk;
            if (chunk >= 0.5 * plan) {
                double k = took / chunk;
                // teurer geworden: schnell reagieren; billiger: langsam nachgeben
                cost = k > cost ? 0.4 * cost + 0.6 * k : 0.85 * cost + 0.15 * k;
                cost = std::clamp(cost, 1e-5, 10.0);
            }
            if (!keepGoing(pl)) break;
            // nie langsamer als normal, sonst nicht mehr Rechenzeit als eingestellt
            if (nowSec() - t0 > budget && done >= real) break;
        }
        if (PlayLayer::get() == pl) showSpeed(pl, done / real);
    }

    static void showSpeed(PlayLayer* pl, double speed) {
        shown = shown > 0 ? 0.85 * shown + 0.15 * speed : speed;
        auto label = static_cast<CCLabelBMFont*>(pl->getChildByID("speed"_spr));
        if (!flag("show-speed")) {
            if (label) label->setVisible(false);
            return;
        }
        if (!label) {
            label = CCLabelBMFont::create("Turbo", "bigFont.fnt");
            label->setID("speed"_spr);
            label->setAnchorPoint({1.f, 0.f});
            label->setScale(0.3f);
            label->setOpacity(170);
            label->setPosition({CCDirector::get()->getWinSize().width - 6.f, 5.f});
            pl->addChild(label, 1001);
        }
        label->setVisible(true);
        double now = nowSec();
        if (now - tLabel > 0.25) {
            tLabel = now;
            label->setString(fmt::format("Turbo {}x", static_cast<long>(std::lround(shown))).c_str());
        }
    }

    // kein Turbo in diesem Bild: Ton zurueck, Anzeige weg
    static void idle(GJBaseGameLayer* gl) {
        if (!running && !muted) return;
        running = false;
        lastFrame = 0;
        shown = 0;
        mute(false);
        auto pl = PlayLayer::get();
        if (pl && static_cast<GJBaseGameLayer*>(pl) == gl) {
            if (auto label = pl->getChildByID("speed"_spr)) label->setVisible(false);
        }
    }

    static void reset(bool newLevel) {
        running = false;
        lastFrame = 0;
        shown = 0;
        botStopped = false;
        mute(false);
        if (newLevel) cost = 0.01;
    }
};

}  // namespace

// ---------- Kern: pro Bild so viele Spielschritte, wie in die Rechenzeit passen ----------
class $modify(TurboLayer, GJBaseGameLayer) {
    static void onModify(auto& self) {
        // vor allen anderen Mods: jedes Turbo-Stueck laeuft fuer sie (z. B. den Demon Bot) wie ein normales Bild
        if (self.setHookPriority("GJBaseGameLayer::update", Priority::First).isErr()) {
            log::warn("Hook-Prioritaet fuer GJBaseGameLayer::update konnte nicht gesetzt werden");
        }
    }

    void update(float dt) {
        if (Turbo::depth == 0) {
            auto st = Turbo::state(this);
            if (st == Turbo::State::Run) {
                Turbo::depth++;
                Turbo::frame(this);
                Turbo::depth--;
                return;
            }
            Turbo::idle(this);
            if (st == Turbo::State::PowerOnly) Power::use();   // Platformer-Suche bekommt trotzdem volle Leistung
        }
        Turbo::depth++;
        GJBaseGameLayer::update(dt);
        Turbo::depth--;
    }
};

class $modify(TurboPlayLayer, PlayLayer) {
    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        Turbo::reset(true);
        return PlayLayer::init(level, useReplay, dontCreateObjects);
    }

    void onQuit() {
        Turbo::reset(false);
        PlayLayer::onQuit();
    }

    void pauseGame(bool unfocused) {
        // ESC haelt den Demon Bot an: danach in diesem Level kein Turbo mehr (Hintergrund-Pausen zaehlen nicht)
        if (!unfocused && PlayLayer::get() == this) Turbo::botStopped = true;
        PlayLayer::pauseGame(unfocused);
    }
};

// ---------- Volle Leistung 20 s nach dem letzten Turbo-Bild wieder abgeben ----------
class $modify(TurboScheduler, CCScheduler) {
    void update(float dt) {
        CCScheduler::update(dt);
        Power::tick();
    }
};

$on_mod(Loaded) {
#ifdef GEODE_IS_WINDOWS
    win::planRestore(true);   // GD wurde beim letzten Mal mit "Hoechstleistung" beendet (Absturz): zurueckstellen
#endif
    listenForSettingChanges<bool>("windows-boost", [](bool) { Power::reapply(); });
    listenForSettingChanges<bool>("power-plan", [](bool) { Power::reapply(); });
    listenForKeybindSettingPresses("toggle-key", [](Keybind const&, bool down, bool repeat, double) {
        if (!down || repeat) return false;
        bool on = !flag("enabled");
        Mod::get()->setSettingValue<bool>("enabled", on);
        Notification::create(on ? "Turbo an" : "Turbo aus", NotificationIcon::Info, 1.f)->show();
        return true;
    });
}

// Spiel speichert (z. B. beim Beenden): Energiesparplan sofort zurueck; laeuft der Turbo weiter, kommt er gleich wieder
$on_mod(DataSaved) {
    Power::set(false, false);
}
