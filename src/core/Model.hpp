// SkillFarm PRO – Datenmodell (ohne Geode, damit es sich auf dem PC testen laesst)
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include <matjson.hpp>

namespace sf {

// Spielmodi, in denen gestorben/gespielt wird
enum class Mode : int { Cube = 0, Ship, Ball, Ufo, Wave, Robot, Spider, Swing, Count };
constexpr int MODES = static_cast<int>(Mode::Count);
char const* modeKey(Mode m);    // fuer die Datei
char const* modeName(Mode m);   // fuer die Anzeige

// Art des Versuchs
enum class RunKind : int { Normal = 0, Practice, StartPos, Count };
constexpr int KINDS = static_cast<int>(RunKind::Count);

constexpr int BINS = 100;              // Todes-Statistik in 1-%-Schritten
constexpr std::size_t MAX_RUNS = 300;  // so viele Versuche pro Level werden einzeln gemerkt
constexpr std::size_t MAX_SESSIONS = 400;
constexpr std::size_t RECENT = 10;     // fuer die Erfolgsquote im Drill

// Ein Versuch (fuer spaetere Auswertungen wie Konstanz)
struct Run {
    int64_t time = 0;      // Unix-Sekunden beim Start
    float from = 0;        // Startprozent (0 bei normal, Checkpoint bei Uebung)
    float to = 0;          // erreichtes Prozent
    RunKind kind = RunKind::Normal;
    bool died = false;     // gestorben (sonst geschafft oder abgebrochen)
    bool done = false;     // Level geschafft
};

// Ein Trainingsabschnitt im Drill
struct Section {
    int from = 0;               // Prozent
    int to = 0;                 // Prozent (exklusiv)
    double speed = 0;           // aktuelles Trainingstempo (0 = noch nie trainiert)
    int streak = 0;             // aktuelle Serie sauberer Durchgaenge
    int tries = 0;              // Versuche insgesamt
    int success = 0;            // davon geschafft
    std::deque<bool> recent;    // letzte Ergebnisse
    double masteredSpeed = 0;   // hoechstes Tempo, bei dem der Abschnitt gemeistert wurde
    int64_t masteredAt = 0;
};

struct LevelStats {
    std::string name;
    int levelID = 0;
    int stars = 0;
    int demonDifficulty = -1;   // -1 = kein Demon
    bool platformer = false;

    std::array<int, KINDS> attempts{};
    std::array<float, KINDS> best{};                 // bestes erreichtes Prozent je Art
    std::array<std::array<int, BINS>, 2> deaths{};   // [0] normal, [1] Uebung/Startpos (nur ehrliche Laeufe bei 1,0x)
    std::array<int, MODES> modeDeaths{};
    std::array<double, MODES> modeTime{};            // Spielsekunden je Modus (ehrliche Laeufe bei 1,0x)
    double playTime = 0;                             // echte Sekunden im Level
    int flaggedRuns = 0;                             // mit Noclip/Speedhack, nicht gezaehlt

    bool completedLegit = false;   // selbst geschafft (gemessen: normal, ohne Hilfen)
    int64_t completedAt = 0;
    int attemptsAtCompletion = 0;
    bool manualCompleted = false;  // von Hand als selbst geschafft markiert

    std::vector<Section> sections;
    std::deque<Run> runs;
    int64_t lastPlayed = 0;

    bool selfCompleted() const { return completedLegit || manualCompleted; }
    int totalAttempts() const { return attempts[0] + attempts[1] + attempts[2]; }
    std::array<int, BINS> allDeaths() const;
    int totalDeaths() const;
    void addRun(Run const& r);
    Section* findSection(int from, int to);
    Section& section(int from, int to);   // vorhandenen holen oder anlegen
};

struct Session {
    int64_t start = 0;
    double seconds = 0;   // aktive Spielzeit
    int attempts = 0;
    int deaths = 0;
};

struct Database {
    std::map<std::string, LevelStats> levels;
    std::vector<Session> sessions;

    LevelStats& level(std::string const& key) { return levels[key]; }
    LevelStats const* find(std::string const& key) const;

    // Sitzung fortsetzen oder neu beginnen (neu nach gapSec ohne Spielen)
    Session& session(int64_t now, int64_t lastActivity, int64_t gapSec);

    matjson::Value toJson() const;
    static Database fromJson(matjson::Value const& v);
};

}  // namespace sf
