// SkillFarm PRO – gemeinsamer Zustand zwischen Aufzeichnung (main.cpp) und Oberflaeche (Ui.cpp, ui/*)
#pragma once

#include "core/Analysis.hpp"
#include "core/Engine.hpp"
#include "core/Model.hpp"

#include <Geode/Geode.hpp>

#include <string>
#include <utility>
#include <vector>

namespace sf {

// ---- Datenbank ----
Database& db();
void saveDb();
void markDirty();
int64_t unixNow();
double wallNow();
int tzOffset();   // Sekunden Abstand der Ortszeit zu UTC

// Aufzeichnung nur fuer den eingestellten Account (leer = alle)
bool recordingAllowed();
std::string accountSetting();

std::string levelKey(GJGameLevel* level);
DrillConfig drillConfig();
int coachGoal();
int sectionLength();
int leadIn();

// ---- laufendes Level ----
struct Live {
    PlayLayer* layer = nullptr;   // nur zum Vergleich mit PlayLayer::get(), nie direkt benutzen
    std::string key;
    bool recording = false;
    bool platformer = false;

    // aktueller Versuch
    bool runActive = false;
    float runFrom = 0;
    float runMax = 0;
    RunKind kind = RunKind::Normal;
    int64_t runStart = 0;
    bool runFlagged = false;      // Noclip/Speedhack in diesem Versuch: zaehlt nicht
    bool runSlowed = false;       // Versuch lief (teilweise) mit Drill-Tempo

    // Erkennung von Hilfen
    double wallLast = 0;
    double lastWall = 0;          // echte Zeit des letzten Bildes (0 = unbrauchbar)
    double ratio = 1.0;
    int offFrames = 0;
    bool speedhack = false;
    bool noclipWarned = false;
    bool speedhackWarned = false;

    // Drill
    bool drill = false;
    int secFrom = 0;
    int secTo = 0;
    CheckpointObject* drillCp = nullptr;   // unser Checkpoint (von uns retained)
    bool sectionDone = false;              // Abschnitt geschafft, Neustart steht aus
    bool needsReset = false;               // nach dem Einschalten: zum Anlauf zuruecksetzen
    bool pendingReset = false;             // Neustart fuer das naechste Bild eingeplant
    int autoFrom = -1;                     // Drill, der beim Levelstart automatisch beginnen soll
    int autoTo = -1;
    double speed = 1.0;                    // gerade angewendetes Tempo
    float appliedPitch = 1.f;

    double lastSave = 0;
    bool shapeDone = false;       // Level-Aufbau gelesen (oder schon bekannt)
    bool heatmapReady = false;    // Waermekarte angelegt (oder abgeschaltet)
    std::string hudText;
    std::string sessionText;
};

Live& live();
bool isCurrent(PlayLayer* pl);   // pl ist das laufende, aufgezeichnete Level
bool inLevel();

// Drill steuern (aus dem Pause-Fenster oder dem Dashboard)
void drillEnable(PlayLayer* pl, int from, int to);
void drillDisable(PlayLayer* pl);
Section* drillSection();   // aktueller Abschnitt in der Datenbank (oder nullptr)
void requestDrill(std::string const& key, int from, int to);   // beim naechsten Start dieses Levels

// ---- Session Manager ----
struct PlanRun {
    bool active = false;
    bool paused = false;
    int minutes = 0;
    std::vector<PlanStep> steps;
    double elapsed = 0;     // vergangene Sekunden (ohne Pausen)
    double lastTick = 0;
    int lastPhase = -1;
    Totals start;
    int64_t startUnix = 0;
};
PlanRun& planRun();
void planStart(int minutes);
void planTogglePause();
void planStop(bool completed);
void planTick();
std::string planHint(Phase p);   // was in dieser Phase konkret zu tun ist

// ---- Level-Bibliothek analysieren (fuer Empfehlungen) ----
struct LibraryScan {
    std::vector<std::pair<int, bool>> queue;   // Level-ID, offizielles Level
    std::size_t total = 0;
    std::size_t done = 0;
    bool running = false;
    bool finished = false;
};
LibraryScan& libraryScan();
void libraryScanStart();
void libraryScanStep(double budgetSec);

// ---- Level oeffnen / online suchen (nur ausserhalb eines Levels) ----
void openLevel(std::string const& key, int levelID, bool main);
void openSearch(int tier);

void takeSnapshot();

// ---- Oberflaeche ----
void buildHeatmap(PlayLayer* pl);
void refreshHeatmap(PlayLayer* pl);
void updateHud(PlayLayer* pl);
void addPauseButton(cocos2d::CCLayer* pause);
void addMenuButton(cocos2d::CCLayer* menu);
void showToast(std::string const& text);
void playSound(char const* file);
void showDashboard(int tab);
void showLevelPopup();

}  // namespace sf
