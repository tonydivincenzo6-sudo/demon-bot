// Unit-Tests fuer die Kernlogik (laeuft auf dem PC, nicht Teil der Mod)
#include "../src/core/Analysis.hpp"
#include "../src/core/Model.hpp"

#include <cmath>
#include <cstdio>
#include <string>

using namespace sf;

static int fails = 0;
static int checks = 0;
#define CHECK(cond)                                                         \
    do {                                                                    \
        checks++;                                                           \
        if (!(cond)) {                                                      \
            fails++;                                                        \
            std::printf("FEHLER %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                   \
    } while (0)

static bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) < eps; }

static void testRoundTrip() {
    Database db;
    auto& l = db.level("online-12345");
    l.name = "Deadlocked \"Test\"";
    l.levelID = 12345;
    l.stars = 10;
    l.demonDifficulty = 4;
    l.platformer = false;
    l.attempts = {12, 340, 5};
    l.best = {47.5f, 100.f, 63.f};
    l.deaths[0][47] = 9;
    l.deaths[1][50] = 21;
    l.deaths[1][99] = 2;
    l.modeDeaths[static_cast<int>(Mode::Wave)] = 30;
    l.modeTime[static_cast<int>(Mode::Wave)] = 123.4;
    l.playTime = 3600.5;
    l.flaggedRuns = 3;
    l.completedLegit = true;
    l.completedAt = 1762600000;
    l.attemptsAtCompletion = 112;
    l.manualCompleted = true;
    auto& s = l.section(42, 51);
    s.speed = 0.85;
    s.streak = 2;
    s.tries = 17;
    s.success = 9;
    s.recent = {true, false, true};
    s.masteredSpeed = 1.0;
    s.masteredAt = 1762600100;
    Run r;
    r.time = 1762600200;
    r.from = 12.3f;
    r.to = 48.9f;
    r.kind = RunKind::Practice;
    r.died = true;
    l.addRun(r);
    Run r2 = r;
    r2.kind = RunKind::Normal;
    r2.died = false;
    r2.done = true;
    r2.to = 100.f;
    l.addRun(r2);
    l.lastPlayed = 1762600300;
    db.sessions.push_back({1762500000, 1800.5, 40, 35});

    auto text = db.toJson().dump();
    auto parsed = matjson::parse(text);
    CHECK(parsed.isOk());
    auto db2 = Database::fromJson(parsed.unwrap());
    auto const* l2 = db2.find("online-12345");
    CHECK(l2 != nullptr);
    if (!l2) return;
    CHECK(l2->name == l.name);
    CHECK(l2->levelID == 12345);
    CHECK(l2->stars == 10);
    CHECK(l2->demonDifficulty == 4);
    CHECK(!l2->platformer);
    CHECK(l2->attempts[0] == 12 && l2->attempts[1] == 340 && l2->attempts[2] == 5);
    CHECK(near(l2->best[0], 47.5) && near(l2->best[1], 100) && near(l2->best[2], 63));
    CHECK(l2->deaths[0][47] == 9 && l2->deaths[1][50] == 21 && l2->deaths[1][99] == 2);
    CHECK(l2->totalDeaths() == 32);
    CHECK(l2->modeDeaths[static_cast<int>(Mode::Wave)] == 30);
    CHECK(near(l2->modeTime[static_cast<int>(Mode::Wave)], 123.4, 0.051));
    CHECK(near(l2->playTime, 3600.5, 0.051));
    CHECK(l2->flaggedRuns == 3);
    CHECK(l2->completedLegit && l2->completedAt == 1762600000 && l2->attemptsAtCompletion == 112);
    CHECK(l2->manualCompleted && l2->selfCompleted());
    CHECK(l2->sections.size() == 1);
    if (!l2->sections.empty()) {
        auto const& s2 = l2->sections[0];
        CHECK(s2.from == 42 && s2.to == 51);
        CHECK(near(s2.speed, 0.85));
        CHECK(s2.streak == 2 && s2.tries == 17 && s2.success == 9);
        CHECK(s2.recent.size() == 3 && s2.recent[0] && !s2.recent[1] && s2.recent[2]);
        CHECK(near(s2.masteredSpeed, 1.0) && s2.masteredAt == 1762600100);
    }
    CHECK(l2->runs.size() == 2);
    if (l2->runs.size() == 2) {
        CHECK(l2->runs[0].kind == RunKind::Practice && l2->runs[0].died && !l2->runs[0].done);
        CHECK(near(l2->runs[0].from, 12.3, 0.051) && near(l2->runs[0].to, 48.9, 0.051));
        CHECK(l2->runs[1].kind == RunKind::Normal && !l2->runs[1].died && l2->runs[1].done);
    }
    CHECK(l2->lastPlayed == 1762600300);
    CHECK(db2.sessions.size() == 1);
    if (!db2.sessions.empty()) {
        CHECK(db2.sessions[0].start == 1762500000 && db2.sessions[0].attempts == 40 && db2.sessions[0].deaths == 35);
        CHECK(near(db2.sessions[0].seconds, 1800.5, 0.051));
    }
}

static void testBrokenInput() {
    // falsche Typen, fehlende Felder, Muell: darf nichts kaputt machen
    char const* inputs[] = {
        "{}",
        "[]",
        "{\"levels\": 5}",
        "{\"levels\": {\"x\": 5, \"\": {}, \"y\": {\"att\": \"nein\", \"dn\": [1,2,\"a\"], \"sec\": [{\"f\": 50, \"t\": 10}], \"runs\": [[1],[1,2,3,9,1]]}}}",
        "{\"sessions\": [[1], \"a\", [1, 2, 3, 4]]}",
    };
    for (auto in : inputs) {
        auto p = matjson::parse(in);
        CHECK(p.isOk());
        if (p.isErr()) continue;
        auto db = Database::fromJson(p.unwrap());
        (void)db.toJson().dump();
    }
    auto p = matjson::parse(inputs[3]).unwrap();
    auto db = Database::fromJson(p);
    CHECK(db.levels.size() == 1);   // "x" (keine Tabelle) und "" werden uebersprungen
    auto const* y = db.find("y");
    CHECK(y != nullptr);
    if (y) {
        CHECK(y->attempts[0] == 0);
        CHECK(y->deaths[0][0] == 1 && y->deaths[0][1] == 2 && y->deaths[0][2] == 0);
        CHECK(y->sections.empty());                 // from >= to wird verworfen
        CHECK(y->runs.size() == 1);                 // zu kurzer Eintrag wird verworfen
        if (!y->runs.empty()) CHECK(y->runs[0].kind == RunKind::StartPos);   // 9 wird auf gueltigen Wert begrenzt
    }
    auto p2 = matjson::parse(inputs[4]).unwrap();
    CHECK(Database::fromJson(p2).sessions.size() == 1);
}

static void testHotspots() {
    std::array<int, BINS> d{};
    CHECK(hotspots(d, 3, 8, 2).empty());

    d[50] = 10;
    auto h = hotspots(d, 3, 8, 2);
    CHECK(h.size() == 1);
    if (!h.empty()) {
        CHECK(h[0].deaths == 10);
        CHECK(near(h[0].share, 1.0));
        CHECK(h[0].from <= 50 && h[0].to > 50);
        CHECK(h[0].from >= 44 && h[0].to <= 56);   // Tod liegt etwa mittig
        CHECK(h[0].to - h[0].from == 10);          // 8 Kern + 2 Anlauf
    }

    d = {};
    d[20] = 3;
    d[21] = 4;
    d[70] = 9;
    d[99] = 1;
    h = hotspots(d, 3, 8, 2);
    CHECK(h.size() == 3);
    if (h.size() == 3) {
        CHECK(h[0].deaths == 9 && h[0].from <= 70 && h[0].to > 70);
        CHECK(h[1].deaths == 7 && h[1].from <= 20 && h[1].to > 21);
        CHECK(h[2].deaths == 1 && h[2].to == 100 && h[2].from <= 99 - 0);
        for (int a = 0; a < 3; a++) {
            CHECK(h[a].from >= 0 && h[a].to <= 100 && h[a].to > h[a].from);
        }
    }

    d = {};
    d[0] = 5;   // am Rand
    h = hotspots(d, 1, 8, 2);
    CHECK(h.size() == 1 && !h.empty() && h[0].from == 0 && h[0].to == 8);

    // mehr Stellen gewuenscht als Tode vorhanden: kein Absturz, keine leeren Stellen
    d = {};
    d[30] = 2;
    h = hotspots(d, 5, 8, 2);
    CHECK(h.size() == 1);
}

static void testModeRates() {
    std::array<int, MODES> deaths{};
    std::array<double, MODES> secs{};
    deaths[static_cast<int>(Mode::Cube)] = 10;
    secs[static_cast<int>(Mode::Cube)] = 600;   // 10 min -> 1/min
    deaths[static_cast<int>(Mode::Wave)] = 12;
    secs[static_cast<int>(Mode::Wave)] = 120;   // 2 min -> 6/min
    deaths[static_cast<int>(Mode::Ship)] = 5;
    secs[static_cast<int>(Mode::Ship)] = 20;    // zu kurz
    auto r = modeRates(deaths, secs, 0.5);
    CHECK(r.size() == 2);
    if (r.size() == 2) {
        CHECK(r[0].mode == Mode::Wave && near(r[0].perMinute, 6.0));
        CHECK(r[1].mode == Mode::Cube && near(r[1].perMinute, 1.0));
    }
}

static void testDrill() {
    DrillConfig c;   // 0,8 -> 1,0 in 0,05er Schritten, Serie 3, Meister 5
    c = c.sanitized();
    Section s;
    s.from = 40;
    s.to = 50;
    drillPrepare(s, c);
    CHECK(near(s.speed, 0.8));
    CHECK(drillGoal(s, c) == 3);

    // 2 geschafft, 1 gestorben: Serie weg, kein Tempowechsel
    CHECK(drillAttempt(s, true, c, 1) == DrillEvent::None);
    CHECK(drillAttempt(s, true, c, 1) == DrillEvent::None);
    CHECK(drillAttempt(s, false, c, 1) == DrillEvent::None);
    CHECK(s.streak == 0 && near(s.speed, 0.8));

    // 3 in Folge: schneller
    drillAttempt(s, true, c, 1);
    drillAttempt(s, true, c, 1);
    CHECK(drillAttempt(s, true, c, 1) == DrillEvent::SpeedUp);
    CHECK(near(s.speed, 0.85) && s.streak == 0 && s.recent.empty());

    // bis zum Zieltempo hochspielen
    int guard = 0;
    while (s.speed < 1.0 - 1e-9 && guard++ < 100) drillAttempt(s, true, c, 2);
    CHECK(near(s.speed, 1.0));
    CHECK(drillGoal(s, c) == 5);
    CHECK(!drillMastered(s, c));

    // beim Zieltempo braucht es 5 in Folge
    for (int i = 0; i < 4; i++) CHECK(drillAttempt(s, true, c, 3) == DrillEvent::None);
    CHECK(drillAttempt(s, true, c, 3) == DrillEvent::Mastered);
    CHECK(drillMastered(s, c) && s.masteredAt == 3);
    // erneut 5 in Folge: keine zweite Meldung
    for (int i = 0; i < 5; i++) CHECK(drillAttempt(s, true, c, 4) == DrillEvent::None);
    CHECK(near(s.speed, 1.0));   // nie ueber das Ziel

    // schlechte Quote: Tempo runter (erst ab 6 Ergebnissen)
    Section t;
    t.from = 10;
    t.to = 20;
    drillPrepare(t, c);
    t.speed = 0.9;
    for (int i = 0; i < 5; i++) CHECK(drillAttempt(t, false, c, 5) == DrillEvent::None);
    CHECK(drillAttempt(t, false, c, 5) == DrillEvent::SpeedDown);
    CHECK(near(t.speed, 0.85) && t.recent.empty());

    // nie unter das Minimum
    t.speed = 0.5;
    for (int i = 0; i < 20; i++) CHECK(drillAttempt(t, false, c, 6) != DrillEvent::SpeedDown);
    CHECK(near(t.speed, 0.5));

    // Ziel gesenkt: Tempo wird beim naechsten Start begrenzt
    Section u;
    u.speed = 1.2;
    drillPrepare(u, c);
    CHECK(near(u.speed, 1.0));

    // brutal: Ziel 1,2
    DrillConfig b = c;
    b.targetSpeed = 1.2;
    b = b.sanitized();
    CHECK(!drillMastered(s, b));   // bei 1,0 gemeistert reicht fuer 1,2 nicht
    Section v;
    drillPrepare(v, b);
    guard = 0;
    while (v.speed < 1.2 - 1e-9 && guard++ < 200) drillAttempt(v, true, b, 7);
    CHECK(near(v.speed, 1.2));

    // kaputte Einstellungen werden begrenzt
    DrillConfig w;
    w.startSpeed = 5;
    w.targetSpeed = 0.1;
    w.step = 0;
    w.streakGoal = 0;
    w = w.sanitized();
    CHECK(w.targetSpeed >= w.minSpeed && w.startSpeed <= w.targetSpeed && w.step > 0 && w.streakGoal >= 1);
}

static void testSessionsAndRuns() {
    Database db;
    auto& a = db.session(1000, 0, 1200);
    a.attempts = 5;
    CHECK(db.sessions.size() == 1);
    db.session(1500, 1400, 1200);   // innerhalb der Luecke: gleiche Sitzung
    CHECK(db.sessions.size() == 1 && db.sessions.back().attempts == 5);
    db.session(5000, 1500, 1200);   // laenger nichts gespielt: neue Sitzung
    CHECK(db.sessions.size() == 2 && db.sessions.back().attempts == 0);
    for (int i = 0; i < 1000; i++) db.session(10000 + i * 5000, 1, 1200);
    CHECK(db.sessions.size() == MAX_SESSIONS);

    LevelStats l;
    for (int i = 0; i < 1000; i++) {
        Run r;
        r.time = i;
        l.addRun(r);
    }
    CHECK(l.runs.size() == MAX_RUNS && l.runs.front().time == 1000 - static_cast<int>(MAX_RUNS));
    auto& s1 = l.section(10, 20);
    s1.tries = 4;
    CHECK(l.section(10, 20).tries == 4);   // gleicher Abschnitt wird wiederverwendet
    CHECK(l.sections.size() == 1);
}

int main() {
    testRoundTrip();
    testBrokenInput();
    testHotspots();
    testModeRates();
    testDrill();
    testSessionsAndRuns();
    std::printf("%d Pruefungen, %d Fehler\n", checks, fails);
    return fails == 0 ? 0 : 1;
}
