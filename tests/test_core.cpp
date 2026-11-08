// Unit-Tests fuer die Kernlogik (laeuft auf dem PC, nicht Teil der Mod)
#include "../src/core/Analysis.hpp"
#include "../src/core/Engine.hpp"
#include "../src/core/LevelScan.hpp"
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

static bool approx(double a, double b, double eps = 1e-6) { return std::fabs(a - b) < eps; }

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
    Session old;
    old.start = 1762500000;
    old.seconds = 1800.5;
    old.attempts = 40;
    old.deaths = 35;
    db.sessions.push_back(old);

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
    CHECK(approx(l2->best[0], 47.5) && approx(l2->best[1], 100) && approx(l2->best[2], 63));
    CHECK(l2->deaths[0][47] == 9 && l2->deaths[1][50] == 21 && l2->deaths[1][99] == 2);
    CHECK(l2->totalDeaths() == 32);
    CHECK(l2->modeDeaths[static_cast<int>(Mode::Wave)] == 30);
    CHECK(approx(l2->modeTime[static_cast<int>(Mode::Wave)], 123.4, 0.051));
    CHECK(approx(l2->playTime, 3600.5, 0.051));
    CHECK(l2->flaggedRuns == 3);
    CHECK(l2->completedLegit && l2->completedAt == 1762600000 && l2->attemptsAtCompletion == 112);
    CHECK(l2->manualCompleted && l2->selfCompleted());
    CHECK(l2->sections.size() == 1);
    if (!l2->sections.empty()) {
        auto const& s2 = l2->sections[0];
        CHECK(s2.from == 42 && s2.to == 51);
        CHECK(approx(s2.speed, 0.85));
        CHECK(s2.streak == 2 && s2.tries == 17 && s2.success == 9);
        CHECK(s2.recent.size() == 3 && s2.recent[0] && !s2.recent[1] && s2.recent[2]);
        CHECK(approx(s2.masteredSpeed, 1.0) && s2.masteredAt == 1762600100);
    }
    CHECK(l2->runs.size() == 2);
    if (l2->runs.size() == 2) {
        CHECK(l2->runs[0].kind == RunKind::Practice && l2->runs[0].died && !l2->runs[0].done);
        CHECK(approx(l2->runs[0].from, 12.3, 0.051) && approx(l2->runs[0].to, 48.9, 0.051));
        CHECK(l2->runs[1].kind == RunKind::Normal && !l2->runs[1].died && l2->runs[1].done);
    }
    CHECK(l2->lastPlayed == 1762600300);
    CHECK(db2.sessions.size() == 1);
    if (!db2.sessions.empty()) {
        CHECK(db2.sessions[0].start == 1762500000 && db2.sessions[0].attempts == 40 && db2.sessions[0].deaths == 35);
        CHECK(approx(db2.sessions[0].seconds, 1800.5, 0.051));
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
        CHECK(approx(h[0].share, 1.0));
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
        CHECK(r[0].mode == Mode::Wave && approx(r[0].perMinute, 6.0));
        CHECK(r[1].mode == Mode::Cube && approx(r[1].perMinute, 1.0));
    }
}

static void testDrill() {
    DrillConfig c;   // 0,8 -> 1,0 in 0,05er Schritten, Serie 3, Meister 5
    c = c.sanitized();
    Section s;
    s.from = 40;
    s.to = 50;
    drillPrepare(s, c);
    CHECK(approx(s.speed, 0.8));
    CHECK(drillGoal(s, c) == 3);

    // 2 geschafft, 1 gestorben: Serie weg, kein Tempowechsel
    CHECK(drillAttempt(s, true, c, 1) == DrillEvent::None);
    CHECK(drillAttempt(s, true, c, 1) == DrillEvent::None);
    CHECK(drillAttempt(s, false, c, 1) == DrillEvent::None);
    CHECK(s.streak == 0 && approx(s.speed, 0.8));

    // 3 in Folge: schneller
    drillAttempt(s, true, c, 1);
    drillAttempt(s, true, c, 1);
    CHECK(drillAttempt(s, true, c, 1) == DrillEvent::SpeedUp);
    CHECK(approx(s.speed, 0.85) && s.streak == 0 && s.recent.empty());

    // bis zum Zieltempo hochspielen
    int guard = 0;
    while (s.speed < 1.0 - 1e-9 && guard++ < 100) drillAttempt(s, true, c, 2);
    CHECK(approx(s.speed, 1.0));
    CHECK(drillGoal(s, c) == 5);
    CHECK(!drillMastered(s, c));

    // beim Zieltempo braucht es 5 in Folge
    for (int i = 0; i < 4; i++) CHECK(drillAttempt(s, true, c, 3) == DrillEvent::None);
    CHECK(drillAttempt(s, true, c, 3) == DrillEvent::Mastered);
    CHECK(drillMastered(s, c) && s.masteredAt == 3);
    // erneut 5 in Folge: keine zweite Meldung
    for (int i = 0; i < 5; i++) CHECK(drillAttempt(s, true, c, 4) == DrillEvent::None);
    CHECK(approx(s.speed, 1.0));   // nie ueber das Ziel

    // schlechte Quote: Tempo runter (erst ab 6 Ergebnissen)
    Section t;
    t.from = 10;
    t.to = 20;
    drillPrepare(t, c);
    t.speed = 0.9;
    for (int i = 0; i < 5; i++) CHECK(drillAttempt(t, false, c, 5) == DrillEvent::None);
    CHECK(drillAttempt(t, false, c, 5) == DrillEvent::SpeedDown);
    CHECK(approx(t.speed, 0.85) && t.recent.empty());

    // nie unter das Minimum
    t.speed = 0.5;
    for (int i = 0; i < 20; i++) CHECK(drillAttempt(t, false, c, 6) != DrillEvent::SpeedDown);
    CHECK(approx(t.speed, 0.5));

    // Ziel gesenkt: Tempo wird beim naechsten Start begrenzt
    Section u;
    u.speed = 1.2;
    drillPrepare(u, c);
    CHECK(approx(u.speed, 1.0));

    // brutal: Ziel 1,2
    DrillConfig b = c;
    b.targetSpeed = 1.2;
    b = b.sanitized();
    CHECK(!drillMastered(s, b));   // bei 1,0 gemeistert reicht fuer 1,2 nicht
    Section v;
    drillPrepare(v, b);
    guard = 0;
    while (v.speed < 1.2 - 1e-9 && guard++ < 200) drillAttempt(v, true, b, 7);
    CHECK(approx(v.speed, 1.2));

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


// ======================= Stufe 2 =======================
static void testLevelScan() {
    // Start im Ship, Cube-Portal bei 500, Wave-Portal bei 750, Laenge 1000
    std::string lvl = "kS38,1_40_2_125|,kA2,1,kA22,0;1,1,2,15,3,15;1,12,2,500,3,30;1,660,2,750.5,3,30;1,8,2,1000,3,15;";
    auto r = scanLevelString(lvl);
    CHECK(r.ok);
    CHECK(!r.platformer);
    CHECK(approx(r.length, 1000));
    CHECK(r.shape.objects == 4);
    CHECK(r.shape.known());
    CHECK(approx(r.shape.share[static_cast<int>(Mode::Ship)], 0.5, 1e-3));
    CHECK(approx(r.shape.share[static_cast<int>(Mode::Cube)], 0.2505, 1e-3));
    CHECK(approx(r.shape.share[static_cast<int>(Mode::Wave)], 0.2495, 1e-3));
    CHECK(r.shape.segments.size() == 3);
    CHECK(r.shape.modeAt(10) == Mode::Ship);
    CHECK(r.shape.modeAt(60) == Mode::Cube);
    CHECK(r.shape.modeAt(99) == Mode::Wave);
    CHECK(r.shape.mainMode() == Mode::Ship);

    // Portal bei x=0 bestimmt den Start, doppelte Portale, Platformer-Flag, Zahlen mit Exponent
    auto r2 = scanLevelString("kA2,0,kA22,1;1,660,2,0,3,15;1,660,2,100;1,13,2,2e2;1,13,2,300;1,1,2,400");
    CHECK(r2.ok && r2.platformer);
    CHECK(r2.shape.segments.size() == 2);
    CHECK(r2.shape.segments.front().mode == Mode::Wave);
    CHECK(approx(r2.shape.share[static_cast<int>(Mode::Wave)], 0.5, 1e-3));
    CHECK(approx(r2.shape.share[static_cast<int>(Mode::Ship)], 0.5, 1e-3));

    // Muell: kein Absturz, ok=false
    char const* bad[] = {"", ";;;", "abc", "kA2,9;1,abc,2,5", "kA2,1;1,12", "kA2,1;,,,;1,-5,2,10"};
    for (auto b : bad) CHECK(!scanLevelString(b).ok);
    auto r3 = scanLevelString("kA2,99;1,1,2,50");   // Startmodus ausserhalb: wird begrenzt
    CHECK(r3.ok && r3.shape.mainMode() == Mode::Swing);

    CHECK(!buildShape({}, Mode::Cube, 0.f, 3).known());
    CHECK(portalMode(1331) == static_cast<int>(Mode::Spider) && portalMode(1933) == static_cast<int>(Mode::Swing) && portalMode(1) == -1);
}

static void testModelV2() {
    Database db;
    auto& l = db.level("main-18");
    l.name = "The Nightmare";
    l.main = true;
    l.manualCompleted = true;
    l.manualAt = 1794000000;
    l.shape = scanLevelString("kA2,0;1,1,2,10;1,13,2,500;1,1,2,1000").shape;
    auto& s = l.section(10, 20);
    s.lastTried = 1794000100;
    Session ss;
    ss.start = 1794000000;
    ss.end = 1794005000;
    ss.addTime(1000);
    ss.addDeath();
    ss.addTime(500);
    ss.addDeath();
    CHECK(ss.blocks.size() == 2);
    CHECK(approx(ss.blocks[0].seconds, 900) && ss.blocks[0].deaths == 0);   // 1000 s: 900 in Block 0, 100 in Block 1
    CHECK(approx(ss.blocks[1].seconds, 600) && ss.blocks[1].deaths == 2);
    CHECK(approx(ss.seconds, 1500) && ss.deaths == 2);
    db.sessions.push_back(ss);
    LevelInfo info;
    info.name = "Wave Hell";
    info.levelID = 777;
    info.stars = 10;
    info.demonDifficulty = 4;
    info.shape = scanLevelString("kA2,4;1,1,2,10;1,1,2,1000").shape;
    db.library["online-777"] = info;
    std::array<float, SKILLS> snap{};
    snap.fill(-1.f);
    snap[4] = 55.5f;
    db.snapshots[20765] = snap;
    PlanResult pr;
    pr.start = 1794000000;
    pr.minutes = 90;
    pr.phasesDone = 5;
    pr.phases = 5;
    pr.attempts = 120;
    pr.deaths = 100;
    pr.playSeconds = 5000;
    pr.mastered = 2;
    pr.completions = 1;
    db.plans.push_back(pr);

    auto db2 = Database::fromJson(matjson::parse(db.toJson().dump()).unwrap());
    auto const* l2 = db2.find("main-18");
    CHECK(l2 && l2->main && l2->manualAt == 1794000000 && l2->shape.known());
    if (l2) {
        CHECK(approx(l2->shape.share[static_cast<int>(Mode::Ship)], 0.5, 1e-3));
        CHECK(l2->shape.modeAt(80) == Mode::Ship);
        CHECK(!l2->sections.empty() && l2->sections[0].lastTried == 1794000100);
    }
    CHECK(db2.sessions.size() == 1 && db2.sessions[0].end == 1794005000 && db2.sessions[0].blocks.size() == 2);
    CHECK(db2.library.size() == 1 && db2.library["online-777"].demonDifficulty == 4 && db2.library["online-777"].shape.known());
    CHECK(db2.shapeOf("online-777") != nullptr && db2.shapeOf("main-18") != nullptr && db2.shapeOf("x") == nullptr);
    CHECK(db2.snapshots.count(20765) == 1 && approx(db2.snapshots[20765][4], 55.5, 0.06) && db2.snapshots[20765][0] < 0);
    CHECK(db2.plans.size() == 1 && db2.plans[0].minutes == 90 && db2.plans[0].mastered == 2 && db2.plans[0].completions == 1);

    // altes Format aus Stufe 1 (Sitzungen mit 4 Werten, keine neuen Felder)
    auto old = matjson::parse("{\"v\":1,\"levels\":{\"online-1\":{\"name\":\"A\",\"att\":[1,2,3]}},\"sessions\":[[100,60,3,2]]}").unwrap();
    auto db3 = Database::fromJson(old);
    CHECK(db3.levels.size() == 1 && !db3.levels["online-1"].shape.known() && db3.levels["online-1"].manualAt == 0);
    CHECK(db3.sessions.size() == 1 && db3.sessions[0].end == 100 && db3.sessions[0].blocks.empty());
    CHECK(db3.library.empty() && db3.snapshots.empty() && db3.plans.empty());
}

static void testTiers() {
    CHECK(difficultyTier(10, 3) == 6 && difficultyTier(10, 4) == 7 && difficultyTier(10, 0) == 8);
    CHECK(difficultyTier(10, 5) == 9 && difficultyTier(10, 6) == 10);
    CHECK(difficultyTier(0, -1) == -1 && difficultyTier(1, -1) == 0 && difficultyTier(2, -1) == 1);
    CHECK(difficultyTier(5, -1) == 3 && difficultyTier(7, -1) == 4 && difficultyTier(9, -1) == 5);
    CHECK(demonIndex(3) == 0 && demonIndex(4) == 1 && demonIndex(0) == 2 && demonIndex(5) == 3 && demonIndex(6) == 4 && demonIndex(-1) == -1);
    CHECK(tierWeight(-1) == 1.0 && tierWeight(10) > tierWeight(6) && tierWeight(6) > tierWeight(5));
    CHECK(std::string(tierName(7)) == "Medium Demon" && std::string(tierName(-1)) == "Unbewertet");
}

static LevelStats makeLevel(char const* name, int stars, int demon, Mode m, double sec, int deaths) {
    LevelStats l;
    l.name = name;
    l.stars = stars;
    l.demonDifficulty = demon;
    l.modeTime[static_cast<int>(m)] = sec;
    l.modeDeaths[static_cast<int>(m)] = deaths;
    l.attempts[0] = deaths + 1;
    l.playTime = sec;
    return l;
}

static void testSkills() {
    Database db;
    // Wave: 10 min Medium Demon (Gewicht 3,2), 100 Tode -> Ueberleben 19,2 s -> 100*(1-e^(-0,768)) = 53,6
    db.levels["a"] = makeLevel("A", 10, 4, Mode::Wave, 600, 100);
    // Cube: 10 min Hard (Gewicht 1,0), 10 Tode -> 60 s -> 90,9
    db.levels["b"] = makeLevel("B", 4, -1, Mode::Cube, 600, 10);
    // Ship: nur 1 Minute -> zu wenig Daten
    db.levels["c"] = makeLevel("C", 4, -1, Mode::Ship, 60, 3);
    auto sp = computeSkills(db);
    CHECK(approx(sp.values[static_cast<int>(Mode::Wave)].score, 100.0 * (1.0 - std::exp(-19.2 / 25.0)), 0.01));
    CHECK(approx(sp.values[static_cast<int>(Mode::Cube)].score, 100.0 * (1.0 - std::exp(-60.0 / 25.0)), 0.01));
    CHECK(sp.values[static_cast<int>(Mode::Ship)].score < 0);
    CHECK(approx(sp.values[static_cast<int>(Mode::Ship)].amount, 1.0));
    CHECK(sp.strongest == static_cast<int>(Mode::Cube) && sp.weakest == static_cast<int>(Mode::Wave));
    CHECK(sp.values[S_CONSISTENCY].score < 0 && sp.values[S_ENDURANCE].score < 0);
    CHECK(sp.playerClass == "Pre-Demon Player");
    CHECK(sp.attempts == 101 + 11 + 4);
    CHECK(weaknessOf(sp, Mode::Wave) > weaknessOf(sp, Mode::Cube));
    CHECK(approx(weaknessOf(sp, Mode::Ship), 0.35));

    // ohne Tode: Wert bleibt endlich und begrenzt
    Database z;
    z.levels["z"] = makeLevel("Z", 4, -1, Mode::Ufo, 300, 0);
    auto sz = computeSkills(z);
    CHECK(sz.values[static_cast<int>(Mode::Ufo)].score > 90 && sz.values[static_cast<int>(Mode::Ufo)].score <= 99);

    // selbst geschafft: Klasse und Demon-Zaehler
    db.levels["d1"] = makeLevel("The Nightmare", 10, 3, Mode::Cube, 10, 0);
    db.levels["d1"].manualCompleted = true;
    db.levels["d1"].manualAt = 100;
    db.levels["d2"] = makeLevel("Deadlocked", 10, 3, Mode::Cube, 10, 0);
    db.levels["d2"].completedLegit = true;
    db.levels["d2"].completedAt = 200;
    sp = computeSkills(db);
    CHECK(sp.demons[0] == 2 && sp.selfCompleted == 2);
    CHECK(sp.playerClass == "Early Demon Player");
    CHECK(sp.lastCompleted == "Deadlocked");
    db.levels["d3"] = makeLevel("X", 10, 4, Mode::Cube, 10, 0);
    db.levels["d3"].manualCompleted = true;
    CHECK(computeSkills(db).playerClass == "Medium Demon Player");

    // Konstanz aus Drill-Quote und Naehe zum Bestwert
    Database k;
    auto& kl = k.levels["k"];
    auto& sec = kl.section(10, 20);
    sec.tries = 20;
    sec.success = 15;   // 75 %
    for (int i = 0; i < 10; i++) {
        Run r;
        r.time = i;
        r.kind = RunKind::Normal;
        r.to = (i % 2) ? 50.f : 25.f;   // Median 37,5, Max 50 -> 75 %
        kl.addRun(r);
    }
    auto skp = computeSkills(k);
    CHECK(approx(skp.values[S_CONSISTENCY].score, 75.0, 0.01));

    // Ausdauer: frueh 0,5 Tode/min, spaet 1,0 Tode/min -> (0,5+0,2)/(1,0+0,2) = 58,3
    Database e;
    Session s1;
    s1.start = 1;
    for (int b = 0; b < 6; b++) {
        Block bl;
        bl.seconds = 900;
        bl.deaths = b < 2 ? 7 : (b >= 4 ? 15 : 10);   // 7/15 = 0,467/min frueh, 15/15 = 1/min spaet
        s1.blocks.push_back(bl);
    }
    e.sessions.push_back(s1);
    auto se = computeSkills(e);
    CHECK(approx(se.values[S_ENDURANCE].score, 100.0 * (14.0 / 30.0 + 0.2) / (1.0 + 0.2), 0.01));
    auto snap = snapshotOf(se);
    CHECK(approx(snap[S_ENDURANCE], se.values[S_ENDURANCE].score, 0.01) && snap[0] < 0);
}

static void testFarmAndCoach() {
    DrillConfig cfg;
    cfg = cfg.sanitized();
    int64_t now = 1794139200;
    Database db;
    // 2 Easy Demons geschafft
    db.levels["d1"] = makeLevel("N", 10, 3, Mode::Cube, 10, 0);
    db.levels["d1"].manualCompleted = true;
    db.levels["d2"] = makeLevel("D", 10, 3, Mode::Cube, 10, 0);
    db.levels["d2"].completedLegit = true;
    // Wave schwach, Cube stark
    db.levels["w"] = makeLevel("W", 4, -1, Mode::Wave, 600, 300);
    db.levels["c"] = makeLevel("C", 4, -1, Mode::Cube, 600, 5);
    auto sp = computeSkills(db);

    auto coach = coachInfo(sp, 3);
    CHECK(coach.current == 0 && coach.count == 2 && !coach.ready && coach.next == 1);
    CHECK(coach.headline == "2/3 Easy Demons geschafft");
    CHECK(coach.detail == "Noch 1 Easy Demon, dann Medium Demons.");
    auto none = coachInfo(SkillProfile{}, 3);
    CHECK(none.current == -1 && none.next == 0);
    SkillProfile three = sp;
    three.demons[0] = 3;
    auto c3 = coachInfo(three, 3);
    CHECK(c3.ready && c3.headline == "3/3 Easy Demons geschafft - bereit fuer Medium Demons!");

    // ohne Drill-Daten: Ziel = Easy Demon (Stufe 6)
    auto fs = farmStatus(db, sp, now, 3);
    CHECK(fs.band == Band::NoData && fs.baseTier == 6 && fs.targetTier == 6);

    // Drill-Quote 30 % -> zu schwer -> Ziel eine Stufe tiefer
    auto& sec = db.levels["w"].section(10, 20);
    sec.lastTried = now - 3600;
    for (int i = 0; i < 10; i++) sec.recent.push_back(i < 3);
    fs = farmStatus(db, sp, now, 3);
    CHECK(fs.band == Band::TooHard && approx(fs.rate, 0.3) && fs.targetTier == 5);
    // 90 % -> neue Herausforderung
    sec.recent.clear();
    for (int i = 0; i < 10; i++) sec.recent.push_back(i < 9);
    fs = farmStatus(db, sp, now, 3);
    CHECK(fs.band == Band::Ready && fs.targetTier == 7);
    // alte Drill-Daten (ueber 14 Tage) zaehlen nicht
    sec.lastTried = now - 20 * 86400;
    CHECK(farmStatus(db, sp, now, 3).band == Band::NoData);
    sec.lastTried = now - 3600;
    sec.recent.clear();
    for (int i = 0; i < 10; i++) sec.recent.push_back(i < 6);   // 60 % -> Konstanz aufbauen
    fs = farmStatus(db, sp, now, 3);
    CHECK(fs.band == Band::Building && fs.targetTier == 6);

    // Level-Empfehlungen aus der Bibliothek
    auto mk = [](char const* name, int stars, int demon, char const* data) {
        LevelInfo i;
        i.name = name;
        i.stars = stars;
        i.demonDifficulty = demon;
        i.shape = scanLevelString(data).shape;
        return i;
    };
    db.library["online-1"] = mk("WaveEasyDemon", 10, 3, "kA2,4;1,1,2,10;1,1,2,1000");      // 100 % Wave, Easy Demon
    db.library["online-2"] = mk("CubeEasyDemon", 10, 3, "kA2,0;1,1,2,10;1,1,2,1000");      // 100 % Cube, Easy Demon
    db.library["online-3"] = mk("WaveExtreme", 10, 6, "kA2,4;1,1,2,10;1,1,2,1000");        // zu schwer
    db.library["online-4"] = mk("Unbewertet", 0, -1, "kA2,4;1,1,2,10;1,1,2,1000");         // ohne Schwierigkeit
    LevelInfo plat = mk("Plat", 10, 3, "kA2,4,kA22,1;1,1,2,10;1,1,2,1000");
    plat.platformer = true;
    db.library["online-5"] = plat;
    db.library["d1"] = mk("N", 10, 3, "kA2,4;1,1,2,10;1,1,2,1000");                         // schon geschafft
    auto picks = recommendLevels(db, sp, fs, 10);
    CHECK(!picks.empty());
    if (!picks.empty()) {
        CHECK(picks[0].key == "online-1");
        CHECK(picks[0].focus == Mode::Wave && approx(picks[0].focusShare, 1.0, 1e-3));
        CHECK(picks[0].reason.find("Wave") != std::string::npos);
    }
    bool hasBad = false;
    for (auto const& p : picks) hasBad |= p.key == "online-4" || p.key == "online-5" || p.key == "d1";
    CHECK(!hasBad);
    bool cubeBelowWave = false;
    for (std::size_t i = 0; i < picks.size(); i++) {
        if (picks[i].key == "online-2") cubeBelowWave = i > 0;
    }
    CHECK(cubeBelowWave);
    CHECK(recommendLevels(db, sp, fs, 1).size() == 1);

    // Abschnitt-Empfehlungen
    auto& w = db.levels["w"];
    w.lastPlayed = now - 100;
    w.deaths[0][45] = 30;
    w.deaths[0][80] = 5;
    w.shape = scanLevelString("kA2,0;1,1,2,10;1,660,2,400;1,1,2,1000").shape;   // ab 40 % Wave
    auto secs = recommendSections(db, sp, fs, 5, 8, 2, cfg, now);
    CHECK(!secs.empty());
    if (!secs.empty()) {
        CHECK(secs[0].key == "w" && secs[0].from <= 45 && secs[0].to > 45 && secs[0].deaths == 30);
        CHECK(secs[0].hasMode && secs[0].mode == Mode::Wave);
    }
    // gemeisterter Abschnitt faellt raus
    if (!secs.empty()) {
        auto& m = w.section(secs[0].from, secs[0].to);
        m.masteredSpeed = 1.0;
        auto secs2 = recommendSections(db, sp, fs, 5, 8, 2, cfg, now);
        bool still = false;
        for (auto const& s : secs2) still |= s.key == "w" && s.from == secs[0].from;
        CHECK(!still);
    }
}

static void testImprovementAndPlans() {
    DrillConfig cfg;
    cfg = cfg.sanitized();
    int64_t now = 1794139200;   // 2026-11-08 12:00 UTC
    Database db;
    auto& l = db.levels["p"];
    l.name = "Projekt";
    // 5 Spieltage: frueher Bestwert 57 %, die letzten 3 Tage nur bis 50 %
    for (int day = 5; day >= 1; day--) {
        for (int i = 0; i < 15; i++) {
            Run r;
            r.kind = RunKind::Normal;
            r.time = now - day * 86400 + i * 60;
            r.to = day >= 4 ? (i == 0 ? 57.f : 30.f) : 50.f;
            r.died = true;
            l.addRun(r);
        }
    }
    auto sp = computeSkills(db);
    auto fs = farmStatus(db, sp, now, 3);
    auto f = improvement(db, sp, fs, now, 0, cfg);
    bool plateau = false;
    for (auto const& x : f) plateau |= x.title.find("Plateau in Projekt") != std::string::npos;
    CHECK(plateau);

    // neuer Bestwert am letzten Tag: kein Plateau
    Run better;
    better.kind = RunKind::Normal;
    better.time = now - 3600;
    better.to = 60.f;
    l.addRun(better);
    f = improvement(db, sp, fs, now, 0, cfg);
    plateau = false;
    for (auto const& x : f) plateau |= x.title.find("Plateau in") != std::string::npos;
    CHECK(!plateau);

    // festgefahrener Abschnitt
    auto& s = l.section(40, 50);
    s.tries = 50;
    s.lastTried = now - 3600;
    for (int i = 0; i < 10; i++) s.recent.push_back(i < 3);
    f = improvement(db, sp, fs, now, 0, cfg);
    bool stuck = false;
    for (auto const& x : f) stuck |= x.title.find("40-50% schon 50x") != std::string::npos;
    CHECK(stuck);

    // leere Datenbank: positive Meldung
    Database empty;
    auto fe = improvement(empty, computeSkills(empty), farmStatus(empty, computeSkills(empty), now, 3), now, 0, cfg);
    CHECK(fe.size() == 1 && !fe[0].problem);

    // Plaene
    auto p90 = makePlan(90);
    CHECK(planMinutes(p90) == 90 && p90.size() == 5 && p90[0].phase == Phase::WarmUp && p90[4].phase == Phase::Review);
    CHECK(planMinutes(makePlan(45)) == 45 && planMinutes(makePlan(60)) == 60 && planMinutes(makePlan(120)) == 120);
    auto st = phaseAt(p90, 0);
    CHECK(st.index == 0 && !st.finished);
    st = phaseAt(p90, 15 * 60 + 5);
    CHECK(st.index == 1 && approx(st.inPhase, 5) && approx(st.phaseLen, 20 * 60));
    st = phaseAt(p90, 90 * 60 + 1);
    CHECK(st.finished && st.index == 4);
    CHECK(phaseAt({}, 10).finished);

    // leichter Tag nach 3 langen Tagen
    Database t;
    for (int d = 1; d <= 3; d++) {
        Session ss;
        ss.start = now - d * 86400;
        ss.seconds = 4000;
        t.sessions.push_back(ss);
    }
    CHECK(recommendedPlan(t, now, 0, 90) == 45);
    t.sessions.pop_back();
    CHECK(recommendedPlan(t, now, 0, 90) == 90);
}

static void testChartsAndTotals() {
    DrillConfig cfg;
    cfg = cfg.sanitized();
    int64_t now = 1794139200;   // 2026-11-08
    CHECK(dayIndex(now, 0) == 20765);
    CHECK(dayIndex(-1, 0) == -1 && dayIndex(0, 0) == 0);
    CHECK(dayIndex(now - 13 * 3600, 3600) == 20765);   // 23:00 Ortszeit am 7. -> mit +1 h schon der 8.? nein: 2026-11-07 23:00 UTC + 1 h = 8.
    int y, m, d;
    civilFromDays(20765, y, m, d);
    CHECK(y == 2026 && m == 11 && d == 8);
    civilFromDays(19782, y, m, d);
    CHECK(y == 2024 && m == 2 && d == 29);
    civilFromDays(0, y, m, d);
    CHECK(y == 1970 && m == 1 && d == 1);
    civilFromDays(-1, y, m, d);
    CHECK(y == 1969 && m == 12 && d == 31);

    Database db;
    Session a;
    a.start = now - 3600;
    a.seconds = 1800;
    a.attempts = 40;
    db.sessions.push_back(a);
    Session b;
    b.start = now - 2 * 86400;
    b.seconds = 600;
    b.attempts = 10;
    db.sessions.push_back(b);
    Session c;
    c.start = now - 9 * 86400;
    c.attempts = 7;
    db.sessions.push_back(c);
    auto pm = playMinutesPerDay(db, now, 0, 14);
    CHECK(pm.size() == 14 && approx(pm[13], 30) && approx(pm[11], 10) && approx(pm[0], 0));
    auto aw = attemptsPerWeek(db, now, 0, 8);
    CHECK(aw.size() == 8 && approx(aw[7], 50) && approx(aw[6], 7));

    auto& l1 = db.levels["x"];
    l1.demonDifficulty = 3;
    l1.stars = 10;
    l1.completedLegit = true;
    l1.completedAt = now - 86400;
    l1.attemptsAtCompletion = 112;
    auto& l2 = db.levels["y"];
    l2.demonDifficulty = 4;
    l2.stars = 10;
    l2.manualCompleted = true;
    l2.manualAt = now - 40 * 86400;   // Ende September
    auto dm = demonsPerMonth(db, now, 0, 12);
    CHECK(dm.size() == 12 && approx(dm[11], 1) && approx(dm[9], 1));
    auto sc = attemptsVsTier(db);
    CHECK(sc.size() == 1 && approx(sc[0].first, 6) && approx(sc[0].second, 112));
    auto& sec = l1.section(1, 5);
    sec.masteredSpeed = 1.0;
    auto t = totals(db, cfg);
    CHECK(t.completions == 2 && t.mastered == 1);

    std::array<float, SKILLS> s1{};
    s1.fill(-1.f);
    s1[0] = 40.f;
    s1[4] = 60.f;
    db.snapshots[20765] = s1;
    db.snapshots[20760] = s1;
    auto hist = skillHistory(db, now, 0, 10, -1);
    CHECK(hist.size() == 10 && approx(hist[9], 50) && approx(hist[4], 50) && hist[8] < 0);
    auto hw = skillHistory(db, now, 0, 10, 4);
    CHECK(approx(hw[9], 60));
}

int main() {
    testRoundTrip();
    testBrokenInput();
    testHotspots();
    testModeRates();
    testDrill();
    testSessionsAndRuns();
    testLevelScan();
    testModelV2();
    testTiers();
    testSkills();
    testFarmAndCoach();
    testImprovementAndPlans();
    testChartsAndTotals();
    std::printf("%d Pruefungen, %d Fehler\n", checks, fails);
    return fails == 0 ? 0 : 1;
}
