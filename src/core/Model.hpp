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
    int64_t lastTried = 0;      // letzter Durchgang (Unix)
};

// Aufbau eines Levels aus den Portalen: Anteil je Spielmodus und ab welchem Prozent welcher Modus
struct Segment {
    float from = 0;             // ab diesem Prozent
    Mode mode = Mode::Cube;
};

struct LevelShape {
    std::array<float, MODES> share{};   // Anteil der Levellaenge je Modus (Summe 1), alles 0 = unbekannt
    std::vector<Segment> segments;
    int objects = 0;

    bool known() const;
    Mode modeAt(float pct) const;
    Mode mainMode() const;
};

// Ein Level aus deiner GD-Bibliothek (gespeichert, aber evtl. nie mit SkillFarm gespielt) fuer Empfehlungen
struct LevelInfo {
    std::string name;
    int levelID = 0;
    int stars = 0;
    int demonDifficulty = -1;
    bool platformer = false;
    bool main = false;          // offizielles Level von RobTop
    LevelShape shape;
};

struct LevelStats {
    std::string name;
    int levelID = 0;
    int stars = 0;
    int demonDifficulty = -1;   // -1 = kein Demon
    bool platformer = false;
    bool main = false;          // offizielles Level von RobTop
    LevelShape shape;           // Aufbau (beim Spielen aus den Portalen gelesen)
    int64_t manualAt = 0;       // wann von Hand als geschafft markiert

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

constexpr double BLOCK_SEC = 15 * 60;   // Sitzungen werden in 15-Minuten-Bloecke aktiver Spielzeit geteilt
constexpr std::size_t MAX_BLOCKS = 64;

struct Block {
    double seconds = 0;
    int deaths = 0;
};

struct Session {
    int64_t start = 0;
    int64_t end = 0;        // letzte Aktivitaet
    double seconds = 0;     // aktive Spielzeit
    int attempts = 0;
    int deaths = 0;
    std::vector<Block> blocks;   // fuer die Ausdauer: Tode je 15 Minuten

    void addTime(double sec);
    void addDeath();
};

// Ergebnis einer geplanten Trainings-Session (Session Manager)
struct PlanResult {
    int64_t start = 0;
    int minutes = 0;        // geplante Dauer
    int phasesDone = 0;
    int phases = 0;
    int attempts = 0;
    int deaths = 0;
    double playSeconds = 0;
    int mastered = 0;       // neu gemeisterte Abschnitte
    int completions = 0;    // neu selbst geschaffte Level
};

constexpr int SKILLS = MODES + 2;          // 8 Modi + Konstanz + Ausdauer
constexpr std::size_t MAX_SNAPSHOTS = 400;
constexpr std::size_t MAX_PLANS = 200;

struct Database {
    std::map<std::string, LevelStats> levels;
    std::vector<Session> sessions;
    std::map<std::string, LevelInfo> library;               // analysierte Level aus der GD-Bibliothek
    std::map<int, std::array<float, SKILLS>> snapshots;     // Tag -> Skill-Werte (-1 = keine Daten)
    std::vector<PlanResult> plans;

    LevelStats& level(std::string const& key) { return levels[key]; }
    LevelStats const* find(std::string const& key) const;
    LevelShape const* shapeOf(std::string const& key) const;   // aus gespielten Leveln oder der Bibliothek

    // Sitzung fortsetzen oder neu beginnen (neu nach gapSec ohne Spielen)
    Session& session(int64_t now, int64_t lastActivity, int64_t gapSec);

    matjson::Value toJson() const;
    static Database fromJson(matjson::Value const& v);
};

}  // namespace sf
