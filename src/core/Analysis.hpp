// SkillFarm PRO – Auswertungen und Drill-Regeln (ohne Geode, auf dem PC getestet)
#pragma once

#include "Model.hpp"

#include <vector>

namespace sf {

// ---------------- Gefaehrlichste Stellen ----------------
struct Hotspot {
    int from = 0;      // Abschnitt fuer den Drill (inkl. Anlauf)
    int to = 0;
    int deaths = 0;    // Tode im Kernbereich
    double share = 0;  // Anteil an allen Toden
};

// Sucht bis zu `count` nicht ueberlappende Stellen mit den meisten Toden.
// window = Laenge des Kernbereichs in %, leadIn = Anlauf davor in %.
std::vector<Hotspot> hotspots(std::array<int, BINS> const& deaths, int count, int window, int leadIn);

// ---------------- Tode pro Minute je Spielmodus ----------------
struct ModeRate {
    Mode mode = Mode::Cube;
    double minutes = 0;
    int deaths = 0;
    double perMinute = 0;
};

// Nur Modi mit mindestens minMinutes Spielzeit, schlechtester zuerst
std::vector<ModeRate> modeRates(std::array<int, MODES> const& deaths, std::array<double, MODES> const& seconds,
                                double minMinutes);

// ---------------- Drill ----------------
struct DrillConfig {
    double startSpeed = 0.8;   // erstes Trainingstempo
    double step = 0.05;        // Tempo-Stufe
    double targetSpeed = 1.0;  // Ziel (ueber 1,0 = brutal)
    double minSpeed = 0.5;
    int streakGoal = 3;        // Serie bis zur naechsten Tempostufe
    int masteryGoal = 5;       // Serie beim Zieltempo = gemeistert
    std::size_t minTries = 6;  // ab so vielen Ergebnissen wird die Quote bewertet
    double lowRate = 0.4;      // darunter: Tempo runter

    DrillConfig sanitized() const;
};

enum class DrillEvent { None, SpeedUp, SpeedDown, Mastered };

// Vor dem Training: Tempo setzen (beim ersten Mal Starttempo) und in die Grenzen holen
void drillPrepare(Section& s, DrillConfig const& c);

// Ein Durchgang ist zu Ende (geschafft oder gestorben)
DrillEvent drillAttempt(Section& s, bool success, DrillConfig const& c, int64_t now);

// Benoetigte Serie beim aktuellen Tempo
int drillGoal(Section const& s, DrillConfig const& c);

bool drillMastered(Section const& s, DrillConfig const& c);

// Quote der letzten Ergebnisse, -1 wenn noch keine
double recentRate(Section const& s);

}  // namespace sf
