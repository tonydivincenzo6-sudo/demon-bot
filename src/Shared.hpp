// SkillFarm PRO – gemeinsamer Zustand zwischen Aufzeichnung (main.cpp) und Oberflaeche (Ui.cpp)
#pragma once

#include "core/Analysis.hpp"
#include "core/Model.hpp"

#include <Geode/Geode.hpp>

#include <string>

namespace sf {

// ---- Datenbank ----
Database& db();
void saveDb();
int64_t unixNow();
double wallNow();

// Aufzeichnung nur fuer den eingestellten Account (leer = alle)
bool recordingAllowed();
std::string accountSetting();

std::string levelKey(GJGameLevel* level);
DrillConfig drillConfig();

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
    double speed = 1.0;                    // gerade angewendetes Tempo
    float appliedPitch = 1.f;

    double lastSave = 0;
    bool heatmapReady = false;    // Waermekarte angelegt (oder abgeschaltet)
    std::string hudText;
};

Live& live();
bool isCurrent(PlayLayer* pl);   // pl ist das laufende, aufgezeichnete Level

// Drill steuern (aus dem Pause-Fenster)
void drillEnable(PlayLayer* pl, int from, int to);
void drillDisable(PlayLayer* pl);
Section* drillSection();   // aktueller Abschnitt in der Datenbank (oder nullptr)

// ---- Oberflaeche (Ui.cpp) ----
void buildHeatmap(PlayLayer* pl);
void refreshHeatmap(PlayLayer* pl);
void updateHud(PlayLayer* pl);
void addPauseButton(cocos2d::CCLayer* pause);
void addMenuButton(cocos2d::CCLayer* menu);
void showToast(std::string const& text);
void playSound(char const* file);

}  // namespace sf
