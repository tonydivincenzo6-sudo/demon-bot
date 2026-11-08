// SkillFarm PRO – Auswertungs-Systeme: Skills, Farm Engine, Demon Coach, Improvement Engine, Session Manager,
// Statistik. Reine Logik ohne Geode, auf dem PC getestet.
#pragma once

#include "Analysis.hpp"
#include "Model.hpp"

#include <string>
#include <utility>
#include <vector>

namespace sf {

// ---------------- Schwierigkeit ----------------
// -1 unbewertet, 0 Auto, 1 Easy, 2 Normal, 3 Hard, 4 Harder, 5 Insane, 6 Easy Demon .. 10 Extreme Demon
int difficultyTier(int stars, int demonDifficulty);
double tierWeight(int tier);
char const* tierName(int tier);
int demonIndex(int demonDifficulty);   // 0 Easy .. 4 Extreme, -1 kein Demon
char const* demonName(int index);      // "Easy Demon" ..
constexpr int DEMON_TIERS = 5;

// ---------------- Skills ----------------
enum SkillId : int { S_CONSISTENCY = MODES, S_ENDURANCE = MODES + 1 };
char const* skillName(int id);
char const* skillTip(int id);   // was man gegen diese Schwaeche trainiert

struct SkillValue {
    double score = -1;    // 0..100, -1 = zu wenig Daten
    double amount = 0;    // Minuten (Modi), Proben (Konstanz), Minuten nach 60 min (Ausdauer)
};

struct SkillProfile {
    std::array<SkillValue, SKILLS> values{};
    int strongest = -1;   // nur Spielmodi
    int weakest = -1;
    int selfCompleted = 0;
    std::array<int, DEMON_TIERS> demons{};
    int highestTier = -1;
    std::string playerClass;
    long attempts = 0;
    long deaths = 0;
    double playSeconds = 0;
    long flagged = 0;
    int levelsPlayed = 0;
    std::string lastCompleted;
    int64_t lastCompletedAt = 0;
};

SkillProfile computeSkills(Database const& db);
std::array<float, SKILLS> snapshotOf(SkillProfile const& sp);
double weaknessOf(SkillProfile const& sp, Mode m);   // 0 = stark, 1 = schwach (unbekannt: 0,35)

// ---------------- Farm Engine ----------------
enum class Band { NoData, TooHard, Building, Farming, Ready };
char const* bandName(Band b);
char const* bandAdvice(Band b);

struct FarmStatus {
    Band band = Band::NoData;
    double rate = -1;     // Erfolgsquote der letzten Drill-Durchgaenge (14 Tage)
    int samples = 0;
    int baseTier = 5;
    int targetTier = 5;
    bool coachReady = false;
};
FarmStatus farmStatus(Database const& db, SkillProfile const& sp, int64_t now, int coachGoal);

struct LevelPick {
    std::string key;
    std::string name;
    int levelID = 0;
    bool main = false;
    int tier = -1;
    double score = 0;
    Mode focus = Mode::Cube;
    double focusShare = 0;
    bool started = false;
    std::string reason;
};
std::vector<LevelPick> recommendLevels(Database const& db, SkillProfile const& sp, FarmStatus const& fs, std::size_t count);

struct SectionPick {
    std::string key;
    std::string name;
    int levelID = 0;
    bool main = false;
    int from = 0;
    int to = 0;
    int deaths = 0;
    bool hasMode = false;
    Mode mode = Mode::Cube;
    double score = 0;
};
std::vector<SectionPick> recommendSections(Database const& db, SkillProfile const& sp, FarmStatus const& fs, std::size_t count,
                                           int window, int leadIn, DrillConfig const& cfg, int64_t now);

// ---------------- Demon Coach ----------------
struct CoachInfo {
    std::array<int, DEMON_TIERS> demons{};
    int current = -1;     // hoechste geschaffte Demon-Stufe, -1 = noch keiner
    int goal = 3;
    int count = 0;
    bool ready = false;
    int next = 0;         // empfohlene naechste Stufe
    std::string headline;
    std::string detail;
};
CoachInfo coachInfo(SkillProfile const& sp, int goal);

// ---------------- Improvement Engine ----------------
struct Finding {
    std::string title;
    std::string tip;
    bool problem = true;
};
std::vector<Finding> improvement(Database const& db, SkillProfile const& sp, FarmStatus const& fs, int64_t now, int tzOffset,
                                 DrillConfig const& cfg);

// ---------------- Session Manager ----------------
enum class Phase { WarmUp, Weakness, Farming, Demon, Review };
struct PlanStep {
    Phase phase = Phase::WarmUp;
    int minutes = 0;
};
std::vector<PlanStep> makePlan(int minutes);
char const* phaseName(Phase p);
int planMinutes(std::vector<PlanStep> const& plan);
int recommendedPlan(Database const& db, int64_t now, int tzOffset, int defaultMinutes);

struct PhaseState {
    int index = 0;
    double inPhase = 0;    // Sekunden in der aktuellen Phase
    double phaseLen = 0;   // Laenge der aktuellen Phase in Sekunden
    bool finished = false;
};
PhaseState phaseAt(std::vector<PlanStep> const& plan, double elapsedSec);

// ---------------- Summen und Diagramme ----------------
struct Totals {
    long attempts = 0;
    long deaths = 0;
    double playSeconds = 0;
    int mastered = 0;
    int completions = 0;
};
Totals totals(Database const& db, DrillConfig const& cfg);

int dayIndex(int64_t unixTime, int tzOffset);   // Tage seit 1970 (Ortszeit)
void civilFromDays(int days, int& year, int& month, int& day);

std::vector<double> playMinutesPerDay(Database const& db, int64_t now, int tzOffset, int days);
std::vector<double> attemptsPerWeek(Database const& db, int64_t now, int tzOffset, int weeks);
std::vector<double> demonsPerMonth(Database const& db, int64_t now, int tzOffset, int months);
std::vector<std::pair<double, double>> attemptsVsTier(Database const& db);
// Verlauf eines Skills (-1 = Durchschnitt der Modi); fehlende Tage = -1
std::vector<double> skillHistory(Database const& db, int64_t now, int tzOffset, int days, int skill);

}  // namespace sf
