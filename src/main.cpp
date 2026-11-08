// SkillFarm PRO – Aufzeichnung, Erkennung von Hilfen und Drill im Spiel
//
// Stufe 1: misst Versuche, Tode (Prozent + Spielmodus), Spielzeit und echte Abschluesse; erkennt Noclip und
// Speedhack und zaehlt solche Versuche nicht; trainiert im Uebungsmodus einen Abschnitt in Schleife mit Serien
// und anpassendem Tempo. Alle Daten bleiben lokal in skillfarm.json im Speicherordner der Mod.

#include "Shared.hpp"

#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>

using namespace geode::prelude;

namespace sf {

namespace {

Database g_db;
bool g_loaded = false;   // nur speichern, wenn das Laden geklappt hat (nie fremde Daten ueberschreiben)
bool g_dirty = false;
int64_t g_lastActivity = 0;
Live g_live;

constexpr int64_t SESSION_GAP = 20 * 60;   // so lange Pause = neue Trainings-Sitzung

std::filesystem::path dataPath() {
    return Mod::get()->getSaveDir() / "skillfarm.json";
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(std::string s) {
    auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

Mode modeOf(PlayerObject* p) {
    if (!p) return Mode::Cube;
    if (p->m_isShip) return Mode::Ship;
    if (p->m_isBird) return Mode::Ufo;
    if (p->m_isBall) return Mode::Ball;
    if (p->m_isDart) return Mode::Wave;
    if (p->m_isRobot) return Mode::Robot;
    if (p->m_isSpider) return Mode::Spider;
    if (p->m_isSwing) return Mode::Swing;
    return Mode::Cube;
}

int binOf(float pct) {
    if (!std::isfinite(pct)) return 0;
    return std::clamp(static_cast<int>(std::floor(pct)), 0, BINS - 1);
}

float percentOf(PlayLayer* pl) {
    float p = pl->getCurrentPercent();
    return std::isfinite(p) ? std::clamp(p, 0.f, 100.f) : 0.f;
}

float checkpointPercent(PlayLayer* pl, CheckpointObject* cp) {
    if (!cp || !cp->m_player1Checkpoint || pl->m_levelLength <= 0.f) return 0.f;
    return std::clamp(cp->m_player1Checkpoint->m_position.x / pl->m_levelLength * 100.f, 0.f, 100.f);
}

bool alive(PlayLayer* pl) {
    return pl->m_player1 && !pl->m_player1->m_isDead && !pl->m_playerDied && !pl->m_inResetDelay;
}

Session& currentSession() {
    auto now = unixNow();
    if (g_db.sessions.empty()) g_db.session(now, 0, SESSION_GAP);
    return g_db.sessions.back();
}

void markDirty() { g_dirty = true; }

std::string speedStr(double s) {
    auto t = fmt::format("{:.2f}x", s);
    std::replace(t.begin(), t.end(), '.', ',');
    return t;
}

void setPitch(float pitch) {
    auto& L = g_live;
    if (std::fabs(L.appliedPitch - pitch) < 0.001f) return;
    auto fm = FMODAudioEngine::sharedEngine();
    FMOD::ChannelGroup* master = nullptr;
    if (fm && fm->m_system && fm->m_system->getMasterChannelGroup(&master) == FMOD_OK && master) {
        master->setPitch(pitch);   // Musik passt sich dem Trainingstempo an
    }
    L.appliedPitch = pitch;
}

void releaseCp() {
    auto& L = g_live;
    if (L.drillCp) {
        L.drillCp->release();
        L.drillCp = nullptr;
    }
}

void resetLive(bool releaseCheckpoint) {
    if (releaseCheckpoint) releaseCp();
    setPitch(1.f);
    g_live = Live{};
}

// ---------------- Versuche ----------------
void startRun(PlayLayer* pl, float pct) {
    auto& L = g_live;
    L.runActive = true;
    L.runFrom = pct;
    L.runMax = pct;
    L.runStart = unixNow();
    L.kind = pl->m_isPracticeMode ? RunKind::Practice : (pl->m_isTestMode ? RunKind::StartPos : RunKind::Normal);
    L.runFlagged = L.speedhack;
    L.runSlowed = false;
}

void endRun(bool died, bool done, float to) {
    auto& L = g_live;
    if (!L.runActive) return;
    L.runActive = false;
    auto& ls = g_db.level(L.key);
    markDirty();
    if (L.runFlagged) {
        ls.flaggedRuns++;
        return;
    }
    ls.attempts[static_cast<int>(L.kind)]++;
    currentSession().attempts++;
    if (L.runSlowed) return;   // mit Drill-Tempo: zaehlt nicht fuer Bestwerte und Verlauf
    auto& best = ls.best[static_cast<int>(L.kind)];
    best = std::max(best, to);
    Run r;
    r.time = L.runStart;
    r.from = L.runFrom;
    r.to = to;
    r.kind = L.kind;
    r.died = died;
    r.done = done;
    ls.addRun(r);
}

// ---------------- Drill ----------------
void drillResult(bool success) {
    auto s = drillSection();
    if (!s) return;
    auto cfg = drillConfig();
    auto ev = drillAttempt(*s, success, cfg, unixNow());
    markDirty();
    if (success) playSound("counter003.ogg");
    switch (ev) {
        case DrillEvent::SpeedUp:
            showToast(fmt::format("Serie geschafft! Tempo jetzt {}", speedStr(s->speed)));
            break;
        case DrillEvent::SpeedDown:
            showToast(fmt::format("Zu viele Fehler: Tempo runter auf {}", speedStr(s->speed)));
            break;
        case DrillEvent::Mastered:
            playSound("highscoreGet02.ogg");
            showToast(fmt::format("Abschnitt {}-{}% gemeistert!", s->from, s->to));
            break;
        case DrillEvent::None:
            break;
    }
}

void removeCheckpointsAfter(PlayLayer* pl, float pct) {
    auto arr = pl->m_checkpointArray;
    if (!arr) return;
    for (int guard = 0; guard < 2000 && arr->count() > 0; guard++) {
        auto cp = static_cast<CheckpointObject*>(arr->lastObject());
        if (checkpointPercent(pl, cp) <= pct + 0.01f) break;
        auto before = arr->count();
        pl->removeCheckpoint(false);
        if (arr->count() >= before) break;
    }
}

void requestReset() {
    auto& L = g_live;
    if (L.pendingReset) return;
    L.pendingReset = true;
    // erst im naechsten Bild: nie mitten in GDs eigenem Update neu starten
    geode::queueInMainThread([] {
        auto& L = g_live;
        auto pl = PlayLayer::get();
        L.pendingReset = false;
        if (!pl || pl != L.layer || pl->m_isPaused) return;   // pausiert: nach dem Fortsetzen erneut
        L.needsReset = false;
        pl->resetLevel();
    });
}

void drillTick(PlayLayer* pl, float pct, bool isAlive) {
    auto& L = g_live;
    if (!L.drill || L.platformer || !pl->m_isPracticeMode) return;

    if (L.needsReset) {
        if (isAlive) requestReset();
        return;
    }

    auto arr = pl->m_checkpointArray;
    if (L.drillCp) {
        if (!arr || !arr->containsObject(L.drillCp)) {
            releaseCp();   // selbst entfernt: Anlauf beginnt von vorn
        } else {
            // Checkpoints, die GD nach unserem gesetzt hat (Taste oder automatisch), wieder entfernen
            for (int guard = 0; guard < 20 && arr->count() > 0 && arr->lastObject() != L.drillCp; guard++) {
                auto before = arr->count();
                pl->removeCheckpoint(false);
                if (arr->count() >= before) break;
            }
            if (!arr->containsObject(L.drillCp)) releaseCp();
        }
    }

    if (!L.drillCp) {
        // Anlauf: am Abschnittsanfang den Drill-Checkpoint setzen
        if (isAlive && !L.pendingReset && pct >= L.secFrom && pct < L.secTo) {
            if (auto cp = pl->markCheckpoint()) {
                cp->retain();
                L.drillCp = cp;
                L.sectionDone = false;
            }
        }
        return;
    }

    if (L.sectionDone) {
        if (isAlive) requestReset();
        return;
    }
    if (isAlive && pct >= L.secTo) {
        L.sectionDone = true;
        if (!L.runFlagged) drillResult(true);
        requestReset();
    }
}

// ---------------- Ereignisse aus den Hooks ----------------
void onLevelEnter(PlayLayer* pl, GJGameLevel* level) {
    // altes Level ohne onQuit verlassen: Checkpoint nicht mehr anfassen (gehoert evtl. einem geloeschten Level)
    resetLive(false);
    auto& L = g_live;
    L.layer = pl;
    if (!level || !g_loaded || !recordingAllowed()) return;
    L.key = levelKey(level);
    if (L.key.empty()) return;
    L.recording = true;
    L.platformer = level->isPlatformer();

    auto& ls = g_db.level(L.key);
    ls.name = std::string(level->m_levelName);
    ls.levelID = level->m_levelID.value();
    ls.stars = level->m_stars.value();
    ls.demonDifficulty = level->m_demon.value() > 0 ? level->m_demonDifficulty : -1;
    ls.platformer = L.platformer;
    auto now = unixNow();
    ls.lastPlayed = now;
    g_db.session(now, g_lastActivity, SESSION_GAP);
    g_lastActivity = now;
    L.lastSave = wallNow();
    markDirty();
    buildHeatmap(pl);
    updateHud(pl);
}

void onLevelLeave(PlayLayer* pl) {
    auto& L = g_live;
    if (L.layer != pl) return;
    if (L.recording) {
        endRun(false, false, L.runMax);
        saveDb();
    }
    resetLive(true);
}

void onBeforeReset(PlayLayer* pl) {
    if (!isCurrent(pl)) return;
    auto& L = g_live;
    endRun(false, false, L.runMax);   // nach Tod/Abschluss schon beendet, sonst Neustart
}

void onAfterReset(PlayLayer* pl) {
    if (!isCurrent(pl)) return;
    auto& L = g_live;
    L.runActive = false;
    L.sectionDone = false;
    L.needsReset = false;
}

void onComplete(PlayLayer* pl) {
    if (!isCurrent(pl)) return;
    auto& L = g_live;
    bool legit = L.runActive && L.kind == RunKind::Normal && !L.runFlagged && !L.runSlowed && !pl->m_isTestMode &&
                 !pl->m_isPracticeMode;
    L.runMax = 100.f;
    endRun(false, true, 100.f);
    if (legit) {
        auto& ls = g_db.level(L.key);
        if (!ls.completedLegit) {
            ls.completedLegit = true;
            ls.completedAt = unixNow();
            ls.attemptsAtCompletion = ls.attempts[static_cast<int>(RunKind::Normal)];
            showToast(fmt::format("Selbst geschafft! Nach {} Versuchen gespeichert.", ls.attemptsAtCompletion));
        }
        markDirty();
        saveDb();
    }
}

void onDeath(PlayLayer* pl, PlayerObject* player) {
    if (!isCurrent(pl)) return;
    auto& L = g_live;
    float pct = percentOf(pl);
    L.runMax = std::max(L.runMax, pct);
    auto& ls = g_db.level(L.key);
    bool honest = L.runActive && !L.runFlagged && !L.runSlowed;
    if (honest) {
        if (!L.platformer) ls.deaths[L.kind == RunKind::Normal ? 0 : 1][binOf(pct)]++;
        ls.modeDeaths[static_cast<int>(modeOf(player))]++;
    }
    if (L.runActive && !L.runFlagged) currentSession().deaths++;
    // Drill: Tod im Abschnitt beendet die Serie
    if (L.drill && L.drillCp && !L.sectionDone && L.runActive && !L.runFlagged && pct < L.secTo) {
        drillResult(false);
    }
    endRun(true, false, pct);
    markDirty();
    refreshHeatmap(pl);
}

void onNoclipHit(PlayLayer* pl) {
    if (!isCurrent(pl)) return;
    auto& L = g_live;
    L.runFlagged = true;
    if (!L.noclipWarned && Mod::get()->getSettingValue<bool>("cheat-warning")) {
        L.noclipWarned = true;
        showToast("Noclip erkannt: diese Versuche zaehlen nicht");
    }
}

// vor GDs Update: Speedhack erkennen, Drill-Tempo anwenden
float beforeUpdate(PlayLayer* pl, float dt) {
    auto& L = g_live;
    double now = wallNow();
    double wall = 0;
    if (L.wallLast > 0) wall = now - L.wallLast;
    L.wallLast = now;
    L.lastWall = (wall > 0 && wall < 0.25) ? wall : 0;

    if (!pl->m_isPaused && dt > 0 && L.lastWall > 0.0005) {
        L.ratio = 0.92 * L.ratio + 0.08 * (static_cast<double>(dt) / L.lastWall);
        if (L.ratio > 1.25 || L.ratio < 0.75) {
            L.offFrames++;
        } else {
            L.offFrames = 0;
            L.speedhack = false;
        }
        if (L.offFrames > 90 && !L.speedhack) {
            L.speedhack = true;
            if (L.runActive) L.runFlagged = true;
            if (!L.speedhackWarned && Mod::get()->getSettingValue<bool>("cheat-warning")) {
                L.speedhackWarned = true;
                showToast("Speedhack erkannt: diese Versuche zaehlen nicht");
            }
        }
    }

    double speed = 1.0;
    if (L.drill && L.drillCp && pl->m_isPracticeMode && !L.platformer) {
        if (auto s = drillSection()) speed = s->speed > 0 ? s->speed : 1.0;
    }
    L.speed = speed;
    if (L.runActive && std::fabs(speed - 1.0) > 1e-6) L.runSlowed = true;
    setPitch(static_cast<float>(speed));
    return static_cast<float>(dt * speed);
}

// nach GDs Update: Zeiten, Versuchsbeginn, Drill
void afterUpdate(PlayLayer* pl, float gameDt) {
    auto& L = g_live;
    if (pl->m_isPaused) return;
    auto& ls = g_db.level(L.key);
    if (L.lastWall > 0) {
        ls.playTime += L.lastWall;
        currentSession().seconds += L.lastWall;
        g_lastActivity = unixNow();
        markDirty();
    }
    bool isAlive = alive(pl);
    float pct = percentOf(pl);
    if (!L.runActive && isAlive && !L.pendingReset && !L.sectionDone && !L.needsReset) startRun(pl, pct);
    if (L.runActive) {
        L.runMax = std::max(L.runMax, pct);
        if (isAlive && !L.runFlagged && !L.runSlowed && gameDt > 0) {
            ls.modeTime[static_cast<int>(modeOf(pl->m_player1))] += gameDt;
        }
    }
    drillTick(pl, pct, isAlive);
    if (!L.heatmapReady) buildHeatmap(pl);
    updateHud(pl);
    // gespeichert wird nie waehrend des Spielens (kein Ruckler): beim Pausieren, Levelende und Verlassen
}

}  // namespace

// ---------------- oeffentliche Funktionen ----------------
Database& db() { return g_db; }
Live& live() { return g_live; }

int64_t unixNow() {
    return static_cast<int64_t>(std::time(nullptr));
}

double wallNow() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void saveDb() {
    if (!g_loaded) return;
    auto res = file::writeStringSafe(dataPath(), g_db.toJson().dump(matjson::NO_INDENTATION));
    if (res.isErr()) {
        log::error("Speichern fehlgeschlagen: {}", res.unwrapErr());
        return;
    }
    g_dirty = false;
}

std::string accountSetting() {
    return trim(Mod::get()->getSettingValue<std::string>("account"));
}

bool recordingAllowed() {
    auto want = accountSetting();
    if (want.empty()) return true;
    auto am = GJAccountManager::get();
    std::string user = am ? std::string(am->m_username) : std::string();
    return lower(trim(user)) == lower(want);
}

std::string levelKey(GJGameLevel* level) {
    if (!level) return {};
    int id = level->m_levelID.value();
    std::string name = level->m_levelName;
    switch (level->m_levelType) {
        case GJLevelType::Main: return fmt::format("main-{}", id);
        case GJLevelType::Editor: return fmt::format("editor-{}", name);
        default: return id > 0 ? fmt::format("online-{}", id) : fmt::format("local-{}", name);
    }
}

DrillConfig drillConfig() {
    auto mod = Mod::get();
    DrillConfig c;
    c.startSpeed = mod->getSettingValue<double>("start-speed");
    c.targetSpeed = mod->getSettingValue<double>("target-speed");
    c.step = mod->getSettingValue<double>("speed-step");
    c.streakGoal = static_cast<int>(mod->getSettingValue<int64_t>("streak-goal"));
    c.masteryGoal = static_cast<int>(mod->getSettingValue<int64_t>("mastery-goal"));
    return c.sanitized();
}

bool isCurrent(PlayLayer* pl) {
    auto& L = g_live;
    return pl && L.recording && L.layer == pl && PlayLayer::get() == pl;
}

Section* drillSection() {
    auto& L = g_live;
    if (!L.recording || L.secTo <= L.secFrom) return nullptr;
    auto it = g_db.levels.find(L.key);
    if (it == g_db.levels.end()) return nullptr;
    return it->second.findSection(L.secFrom, L.secTo);
}

void drillEnable(PlayLayer* pl, int from, int to) {
    auto& L = g_live;
    if (!isCurrent(pl) || L.platformer) return;
    from = std::clamp(from, 0, 99);
    to = std::clamp(to, from + 1, 100);
    releaseCp();
    L.drill = true;
    L.secFrom = from;
    L.secTo = to;
    L.sectionDone = false;
    L.pendingReset = false;
    auto& s = g_db.level(L.key).section(from, to);
    drillPrepare(s, drillConfig());
    markDirty();
    if (!pl->m_isPracticeMode) pl->togglePracticeMode(true);
    removeCheckpointsAfter(pl, static_cast<float>(from));
    // schon hinter dem Abschnittsanfang: nach dem Fortsetzen am letzten Checkpoint davor neu starten
    L.needsReset = percentOf(pl) >= static_cast<float>(from);
    updateHud(pl);
    refreshHeatmap(pl);
}

void drillDisable(PlayLayer* pl) {
    auto& L = g_live;
    L.drill = false;
    L.sectionDone = false;
    L.pendingReset = false;
    L.needsReset = false;
    releaseCp();
    L.speed = 1.0;
    setPitch(1.f);
    if (pl && L.layer == pl) {
        updateHud(pl);
        refreshHeatmap(pl);
    }
}

}  // namespace sf

// =====================================================================
// Hooks
// =====================================================================
class $modify(SFPlayLayer, PlayLayer) {
    static void onModify(auto& self) {
        // ganz aussen: so sehen wir jeden Treffer, auch wenn ein Noclip-Mod den Tod danach verhindert
        if (self.setHookPriority("PlayLayer::destroyPlayer", Priority::First - 500).isErr()) {
            log::warn("Hook-Prioritaet fuer destroyPlayer nicht gesetzt");
        }
    }

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        sf::onLevelEnter(this, level);
        return true;
    }

    void onQuit() {
        sf::onLevelLeave(this);
        PlayLayer::onQuit();
    }

    void resetLevel() {
        sf::onBeforeReset(this);
        PlayLayer::resetLevel();
        sf::onAfterReset(this);
    }

    void levelComplete() {
        PlayLayer::levelComplete();
        sf::onComplete(this);
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        // GDs eigener Anti-Cheat-Test, schon tote Spieler, Levelende: nur durchreichen
        if (!player || object == m_anticheatSpike || player->m_isDead || m_playerDied || m_inResetDelay ||
            m_hasCompletedLevel || m_levelEndAnimationStarted || !sf::isCurrent(this)) {
            PlayLayer::destroyPlayer(player, object);
            return;
        }
        PlayLayer::destroyPlayer(player, object);
        if (player->m_isDead || m_playerDied) {
            sf::onDeath(this, player);
        } else {
            sf::onNoclipHit(this);   // getroffen, aber nicht gestorben
        }
    }
};

class $modify(SFGameLayer, GJBaseGameLayer) {
    void update(float dt) {
        auto pl = PlayLayer::get();
        if (!pl || static_cast<GJBaseGameLayer*>(pl) != this || !sf::isCurrent(pl)) {
            GJBaseGameLayer::update(dt);
            return;
        }
        float scaled = sf::beforeUpdate(pl, dt);
        GJBaseGameLayer::update(scaled);
        if (sf::isCurrent(pl)) sf::afterUpdate(pl, scaled);
    }
};

class $modify(SFPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        sf::addPauseButton(this);
        if (sf::g_dirty) sf::saveDb();   // Spiel steht: guter Moment zum Speichern
    }
};

class $modify(SFMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        sf::addMenuButton(this);
        return true;
    }
};

$on_mod(Loaded) {
    auto path = sf::dataPath();
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        sf::g_loaded = true;   // erster Start: leere Daten
        return;
    }
    auto text = file::readString(path);
    if (text.isErr()) {
        // Datei da, aber nicht lesbar: lieber nichts speichern als sie zu ueberschreiben
        log::error("skillfarm.json nicht lesbar: {}", text.unwrapErr());
        return;
    }
    auto parsed = matjson::parse(text.unwrap());
    if (parsed.isErr()) {
        // kaputt: sichern und neu anfangen
        auto backup = path;
        backup.replace_filename(fmt::format("skillfarm-kaputt-{}.json", sf::unixNow()));
        std::filesystem::copy_file(path, backup, ec);
        log::error("skillfarm.json beschaedigt, gesichert als {}", backup.filename().string());
        sf::g_loaded = !ec;
        return;
    }
    sf::g_db = sf::Database::fromJson(parsed.unwrap());
    sf::g_loaded = true;
}

$on_mod(DataSaved) {
    if (sf::g_dirty) sf::saveDb();
}
