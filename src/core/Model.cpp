#include "Model.hpp"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace sf {

namespace {

char const* const MODE_KEYS[MODES] = {"cube", "ship", "ball", "ufo", "wave", "robot", "spider", "swing"};
char const* const MODE_NAMES[MODES] = {"Cube", "Ship", "Ball", "UFO", "Wave", "Robot", "Spider", "Swing"};

// ---- Lesen mit Standardwerten (fehlende oder kaputte Eintraege brechen nichts) ----
int64_t getInt(matjson::Value const& v, char const* key, int64_t def = 0) {
    if (!v.contains(key)) return def;
    return static_cast<int64_t>(v[key].asInt().unwrapOr(def));
}

double getNum(matjson::Value const& v, char const* key, double def = 0) {
    if (!v.contains(key)) return def;
    double d = v[key].asDouble().unwrapOr(def);
    return std::isfinite(d) ? d : def;
}

bool getBool(matjson::Value const& v, char const* key, bool def = false) {
    if (!v.contains(key)) return def;
    return v[key].asBool().unwrapOr(def);
}

std::string getStr(matjson::Value const& v, char const* key) {
    if (!v.contains(key)) return {};
    return v[key].asString().unwrapOr(std::string());
}

double numAt(matjson::Value const& arr, std::size_t i, double def = 0) {
    if (!arr.isArray() || i >= arr.size()) return def;
    double d = arr[i].asDouble().unwrapOr(def);
    return std::isfinite(d) ? d : def;
}

int64_t intAt(matjson::Value const& arr, std::size_t i, int64_t def = 0) {
    if (!arr.isArray() || i >= arr.size()) return def;
    return static_cast<int64_t>(arr[i].asInt().unwrapOr(def));
}

// Zahlenliste lesen (zu kurze Listen werden mit 0 aufgefuellt, zu lange abgeschnitten)
template <class T, std::size_t N>
void getArray(matjson::Value const& v, char const* key, std::array<T, N>& out) {
    out.fill(T{});
    if (!v.contains(key) || !v[key].isArray()) return;
    auto const& arr = v[key];
    std::size_t n = std::min(arr.size(), N);
    for (std::size_t i = 0; i < n; i++) {
        if constexpr (std::is_floating_point_v<T>) {
            out[i] = static_cast<T>(numAt(arr, i));
        } else {
            out[i] = static_cast<T>(intAt(arr, i));
        }
    }
}

template <class T, std::size_t N>
matjson::Value putArray(std::array<T, N> const& a, double scale = 10.0) {
    auto arr = matjson::Value::array();
    for (auto const& x : a) {
        if constexpr (std::is_floating_point_v<T>) {
            arr.push(matjson::Value(std::round(static_cast<double>(x) * scale) / scale));
        } else {
            arr.push(matjson::Value(static_cast<int64_t>(x)));
        }
    }
    return arr;
}

matjson::Value num(double x, double scale = 10.0) {
    return matjson::Value(std::isfinite(x) ? std::round(x * scale) / scale : 0.0);
}

matjson::Value integer(int64_t x) { return matjson::Value(x); }

Mode toMode(int64_t m) {
    return static_cast<Mode>(std::clamp<int64_t>(m, 0, MODES - 1));
}

// ---------------- Aufbau ----------------
matjson::Value shapeToJson(LevelShape const& s) {
    auto o = matjson::Value::object();
    o.set("s", putArray(s.share, 1000.0));
    auto seg = matjson::Value::array();
    for (auto const& g : s.segments) {
        auto a = matjson::Value::array();
        a.push(num(g.from));
        a.push(integer(static_cast<int>(g.mode)));
        seg.push(a);
    }
    o.set("seg", seg);
    o.set("obj", integer(s.objects));
    return o;
}

LevelShape shapeFromJson(matjson::Value const& o) {
    LevelShape s;
    if (!o.isObject()) return s;
    getArray(o, "s", s.share);
    float sum = 0;
    for (auto& x : s.share) {
        x = std::clamp(x, 0.f, 1.f);
        sum += x;
    }
    if (sum <= 0) s.share.fill(0);
    if (o.contains("seg") && o["seg"].isArray()) {
        auto const& seg = o["seg"];
        for (std::size_t i = 0; i < seg.size() && i < 2000; i++) {
            if (!seg[i].isArray() || seg[i].size() < 2) continue;
            Segment g;
            g.from = static_cast<float>(std::clamp(numAt(seg[i], 0), 0.0, 100.0));
            g.mode = toMode(intAt(seg[i], 1));
            s.segments.push_back(g);
        }
        std::stable_sort(s.segments.begin(), s.segments.end(),
                         [](Segment const& a, Segment const& b) { return a.from < b.from; });
    }
    s.objects = static_cast<int>(std::max<int64_t>(0, getInt(o, "obj")));
    return s;
}

// ---------------- Abschnitte ----------------
double round2(double x) { return std::round(x * 100.0) / 100.0; }

matjson::Value sectionToJson(Section const& s) {
    auto o = matjson::Value::object();
    o.set("f", integer(s.from));
    o.set("t", integer(s.to));
    o.set("sp", matjson::Value(round2(s.speed)));
    o.set("st", integer(s.streak));
    o.set("tr", integer(s.tries));
    o.set("su", integer(s.success));
    std::string rec;
    for (bool b : s.recent) rec += b ? '1' : '0';
    o.set("rec", matjson::Value(rec));
    o.set("ms", matjson::Value(round2(s.masteredSpeed)));
    o.set("msAt", integer(s.masteredAt));
    o.set("lt", integer(s.lastTried));
    return o;
}

Section sectionFromJson(matjson::Value const& o) {
    Section s;
    s.from = static_cast<int>(std::clamp<int64_t>(getInt(o, "f"), 0, 100));
    s.to = static_cast<int>(std::clamp<int64_t>(getInt(o, "t"), 0, 100));
    s.speed = getNum(o, "sp");
    s.streak = static_cast<int>(std::max<int64_t>(0, getInt(o, "st")));
    s.tries = static_cast<int>(std::max<int64_t>(0, getInt(o, "tr")));
    s.success = static_cast<int>(std::clamp<int64_t>(getInt(o, "su"), 0, s.tries));
    for (char c : getStr(o, "rec")) {
        if (c == '0' || c == '1') s.recent.push_back(c == '1');
    }
    while (s.recent.size() > RECENT) s.recent.pop_front();
    s.masteredSpeed = getNum(o, "ms");
    s.masteredAt = getInt(o, "msAt");
    s.lastTried = getInt(o, "lt");
    return s;
}

// ---------------- Level ----------------
matjson::Value levelToJson(LevelStats const& l) {
    auto o = matjson::Value::object();
    o.set("name", matjson::Value(l.name));
    o.set("id", integer(l.levelID));
    o.set("stars", integer(l.stars));
    o.set("demon", integer(l.demonDifficulty));
    o.set("plat", matjson::Value(l.platformer));
    o.set("main", matjson::Value(l.main));
    if (l.shape.known()) o.set("shape", shapeToJson(l.shape));
    o.set("att", putArray(l.attempts));
    o.set("best", putArray(l.best));
    o.set("dn", putArray(l.deaths[0]));
    o.set("dp", putArray(l.deaths[1]));
    o.set("md", putArray(l.modeDeaths));
    o.set("mt", putArray(l.modeTime));
    o.set("time", num(l.playTime));
    o.set("flag", integer(l.flaggedRuns));
    o.set("done", matjson::Value(l.completedLegit));
    o.set("doneAt", integer(l.completedAt));
    o.set("doneAtt", integer(l.attemptsAtCompletion));
    o.set("manual", matjson::Value(l.manualCompleted));
    o.set("manualAt", integer(l.manualAt));
    auto secs = matjson::Value::array();
    for (auto const& s : l.sections) secs.push(sectionToJson(s));
    o.set("sec", secs);
    auto runs = matjson::Value::array();
    for (auto const& r : l.runs) {
        auto a = matjson::Value::array();
        a.push(integer(r.time));
        a.push(num(r.from));
        a.push(num(r.to));
        a.push(integer(static_cast<int>(r.kind)));
        a.push(integer((r.died ? 1 : 0) | (r.done ? 2 : 0)));
        runs.push(a);
    }
    o.set("runs", runs);
    o.set("last", integer(l.lastPlayed));
    return o;
}

LevelStats levelFromJson(matjson::Value const& o) {
    LevelStats l;
    l.name = getStr(o, "name");
    l.levelID = static_cast<int>(getInt(o, "id"));
    l.stars = static_cast<int>(getInt(o, "stars"));
    l.demonDifficulty = static_cast<int>(getInt(o, "demon", -1));
    l.platformer = getBool(o, "plat");
    l.main = getBool(o, "main");
    if (o.contains("shape")) l.shape = shapeFromJson(o["shape"]);
    getArray(o, "att", l.attempts);
    getArray(o, "best", l.best);
    getArray(o, "dn", l.deaths[0]);
    getArray(o, "dp", l.deaths[1]);
    getArray(o, "md", l.modeDeaths);
    getArray(o, "mt", l.modeTime);
    for (auto& a : l.attempts) a = std::max(a, 0);
    for (auto& b : l.best) b = std::clamp(b, 0.f, 100.f);
    for (auto& row : l.deaths) {
        for (auto& d : row) d = std::max(d, 0);
    }
    for (auto& d : l.modeDeaths) d = std::max(d, 0);
    for (auto& t : l.modeTime) t = std::max(t, 0.0);
    l.playTime = std::max(0.0, getNum(o, "time"));
    l.flaggedRuns = static_cast<int>(std::max<int64_t>(0, getInt(o, "flag")));
    l.completedLegit = getBool(o, "done");
    l.completedAt = getInt(o, "doneAt");
    l.attemptsAtCompletion = static_cast<int>(std::max<int64_t>(0, getInt(o, "doneAtt")));
    l.manualCompleted = getBool(o, "manual");
    l.manualAt = getInt(o, "manualAt");
    if (o.contains("sec") && o["sec"].isArray()) {
        auto const& secs = o["sec"];
        for (std::size_t i = 0; i < secs.size(); i++) {
            auto s = sectionFromJson(secs[i]);
            if (s.to > s.from) l.sections.push_back(std::move(s));
        }
    }
    if (o.contains("runs") && o["runs"].isArray()) {
        auto const& runs = o["runs"];
        for (std::size_t i = 0; i < runs.size(); i++) {
            auto const& a = runs[i];
            if (!a.isArray() || a.size() < 5) continue;
            Run r;
            r.time = intAt(a, 0);
            r.from = static_cast<float>(std::clamp(numAt(a, 1), 0.0, 100.0));
            r.to = static_cast<float>(std::clamp(numAt(a, 2), 0.0, 100.0));
            r.kind = static_cast<RunKind>(std::clamp<int64_t>(intAt(a, 3), 0, KINDS - 1));
            int f = static_cast<int>(intAt(a, 4));
            r.died = (f & 1) != 0;
            r.done = (f & 2) != 0;
            l.runs.push_back(r);
        }
        while (l.runs.size() > MAX_RUNS) l.runs.pop_front();
    }
    l.lastPlayed = getInt(o, "last");
    return l;
}

matjson::Value infoToJson(LevelInfo const& i) {
    auto o = matjson::Value::object();
    o.set("name", matjson::Value(i.name));
    o.set("id", integer(i.levelID));
    o.set("stars", integer(i.stars));
    o.set("demon", integer(i.demonDifficulty));
    o.set("plat", matjson::Value(i.platformer));
    o.set("main", matjson::Value(i.main));
    o.set("shape", shapeToJson(i.shape));
    return o;
}

LevelInfo infoFromJson(matjson::Value const& o) {
    LevelInfo i;
    i.name = getStr(o, "name");
    i.levelID = static_cast<int>(getInt(o, "id"));
    i.stars = static_cast<int>(getInt(o, "stars"));
    i.demonDifficulty = static_cast<int>(getInt(o, "demon", -1));
    i.platformer = getBool(o, "plat");
    i.main = getBool(o, "main");
    if (o.contains("shape")) i.shape = shapeFromJson(o["shape"]);
    return i;
}

}  // namespace

char const* modeKey(Mode m) {
    int i = static_cast<int>(m);
    return (i >= 0 && i < MODES) ? MODE_KEYS[i] : "cube";
}

char const* modeName(Mode m) {
    int i = static_cast<int>(m);
    return (i >= 0 && i < MODES) ? MODE_NAMES[i] : "Cube";
}

// ---------------- LevelShape ----------------
bool LevelShape::known() const {
    for (float s : share) {
        if (s > 0) return true;
    }
    return false;
}

Mode LevelShape::modeAt(float pct) const {
    Mode m = segments.empty() ? mainMode() : segments.front().mode;
    for (auto const& g : segments) {
        if (g.from <= pct + 1e-4f) m = g.mode;
        else break;
    }
    return m;
}

Mode LevelShape::mainMode() const {
    int best = 0;
    for (int i = 1; i < MODES; i++) {
        if (share[i] > share[best]) best = i;
    }
    return static_cast<Mode>(best);
}

// ---------------- LevelStats ----------------
std::array<int, BINS> LevelStats::allDeaths() const {
    std::array<int, BINS> out{};
    for (int i = 0; i < BINS; i++) out[i] = deaths[0][i] + deaths[1][i];
    return out;
}

int LevelStats::totalDeaths() const {
    int n = 0;
    for (int i = 0; i < BINS; i++) n += deaths[0][i] + deaths[1][i];
    return n;
}

void LevelStats::addRun(Run const& r) {
    runs.push_back(r);
    while (runs.size() > MAX_RUNS) runs.pop_front();
}

Section* LevelStats::findSection(int from, int to) {
    for (auto& s : sections) {
        if (s.from == from && s.to == to) return &s;
    }
    return nullptr;
}

Section& LevelStats::section(int from, int to) {
    if (auto s = findSection(from, to)) return *s;
    Section s;
    s.from = from;
    s.to = to;
    sections.push_back(std::move(s));
    return sections.back();
}

// ---------------- Session ----------------
void Session::addTime(double sec) {
    if (!(sec > 0) || !std::isfinite(sec)) return;
    // auf die 15-Minuten-Bloecke verteilen (auch ueber eine Blockgrenze hinweg)
    for (int guard = 0; sec > 1e-9 && guard < 1000; guard++) {
        std::size_t idx = static_cast<std::size_t>(seconds / BLOCK_SEC);
        double room = (static_cast<double>(idx) + 1.0) * BLOCK_SEC - seconds;
        double take = std::min(sec, std::max(room, 1e-6));
        seconds += take;
        sec -= take;
        if (idx >= MAX_BLOCKS) continue;
        if (blocks.size() <= idx) blocks.resize(idx + 1);
        blocks[idx].seconds += take;
    }
    if (sec > 0) seconds += sec;
}

void Session::addDeath() {
    deaths++;
    std::size_t idx = static_cast<std::size_t>(seconds / BLOCK_SEC);
    if (idx >= MAX_BLOCKS) return;
    if (blocks.size() <= idx) blocks.resize(idx + 1);
    blocks[idx].deaths++;
}

// ---------------- Database ----------------
LevelStats const* Database::find(std::string const& key) const {
    auto it = levels.find(key);
    return it == levels.end() ? nullptr : &it->second;
}

LevelShape const* Database::shapeOf(std::string const& key) const {
    auto it = levels.find(key);
    if (it != levels.end() && it->second.shape.known()) return &it->second.shape;
    auto li = library.find(key);
    if (li != library.end() && li->second.shape.known()) return &li->second.shape;
    return nullptr;
}

Session& Database::session(int64_t now, int64_t lastActivity, int64_t gapSec) {
    if (sessions.empty() || lastActivity <= 0 || now - lastActivity > gapSec) {
        Session s;
        s.start = now;
        s.end = now;
        sessions.push_back(s);
        while (sessions.size() > MAX_SESSIONS) sessions.erase(sessions.begin());
    }
    return sessions.back();
}

matjson::Value Database::toJson() const {
    auto root = matjson::Value::object();
    root.set("v", integer(2));
    auto lv = matjson::Value::object();
    for (auto const& [key, l] : levels) lv.set(key, levelToJson(l));
    root.set("levels", lv);

    auto ss = matjson::Value::array();
    for (auto const& s : sessions) {
        auto a = matjson::Value::array();
        a.push(integer(s.start));
        a.push(num(s.seconds));
        a.push(integer(s.attempts));
        a.push(integer(s.deaths));
        a.push(integer(s.end));
        auto bl = matjson::Value::array();
        for (auto const& b : s.blocks) {
            auto x = matjson::Value::array();
            x.push(num(b.seconds));
            x.push(integer(b.deaths));
            bl.push(x);
        }
        a.push(bl);
        ss.push(a);
    }
    root.set("sessions", ss);

    auto lib = matjson::Value::object();
    for (auto const& [key, i] : library) lib.set(key, infoToJson(i));
    root.set("library", lib);

    auto snap = matjson::Value::object();
    for (auto const& [day, vals] : snapshots) snap.set(std::to_string(day), putArray(vals));
    root.set("snap", snap);

    auto plans = matjson::Value::array();
    for (auto const& p : this->plans) {
        auto a = matjson::Value::array();
        a.push(integer(p.start));
        a.push(integer(p.minutes));
        a.push(integer(p.phasesDone));
        a.push(integer(p.phases));
        a.push(integer(p.attempts));
        a.push(integer(p.deaths));
        a.push(num(p.playSeconds));
        a.push(integer(p.mastered));
        a.push(integer(p.completions));
        plans.push(a);
    }
    root.set("plans", plans);
    return root;
}

Database Database::fromJson(matjson::Value const& v) {
    Database db;
    if (!v.isObject()) return db;
    if (v.contains("levels") && v["levels"].isObject()) {
        for (auto const& item : v["levels"]) {
            auto key = item.getKey();
            if (!key || key->empty() || !item.isObject()) continue;
            auto& l = db.levels[*key];
            l = levelFromJson(item);
            if (key->rfind("main-", 0) == 0) l.main = true;   // aeltere Dateien kannten das Feld noch nicht
        }
    }
    if (v.contains("sessions") && v["sessions"].isArray()) {
        auto const& ss = v["sessions"];
        for (std::size_t i = 0; i < ss.size(); i++) {
            auto const& a = ss[i];
            if (!a.isArray() || a.size() < 4) continue;
            Session s;
            s.start = intAt(a, 0);
            s.seconds = std::max(0.0, numAt(a, 1));
            s.attempts = static_cast<int>(std::max<int64_t>(0, intAt(a, 2)));
            s.deaths = static_cast<int>(std::max<int64_t>(0, intAt(a, 3)));
            s.end = a.size() > 4 ? intAt(a, 4) : s.start;
            if (a.size() > 5 && a[5].isArray()) {
                auto const& bl = a[5];
                for (std::size_t k = 0; k < bl.size() && k < MAX_BLOCKS; k++) {
                    Block b;
                    b.seconds = std::max(0.0, numAt(bl[k], 0));
                    b.deaths = static_cast<int>(std::max<int64_t>(0, intAt(bl[k], 1)));
                    s.blocks.push_back(b);
                }
            }
            db.sessions.push_back(std::move(s));
        }
        while (db.sessions.size() > MAX_SESSIONS) db.sessions.erase(db.sessions.begin());
    }
    if (v.contains("library") && v["library"].isObject()) {
        for (auto const& item : v["library"]) {
            auto key = item.getKey();
            if (!key || key->empty() || !item.isObject()) continue;
            db.library[*key] = infoFromJson(item);
        }
    }
    if (v.contains("snap") && v["snap"].isObject()) {
        for (auto const& item : v["snap"]) {
            auto key = item.getKey();
            if (!key || key->empty() || !item.isArray()) continue;
            int day = 0;
            try {
                day = std::stoi(*key);
            } catch (...) {
                continue;
            }
            std::array<float, SKILLS> vals{};
            vals.fill(-1.f);
            for (std::size_t k = 0; k < item.size() && k < static_cast<std::size_t>(SKILLS); k++) {
                vals[k] = static_cast<float>(numAt(item, k, -1.0));
            }
            db.snapshots[day] = vals;
        }
        while (db.snapshots.size() > MAX_SNAPSHOTS) db.snapshots.erase(db.snapshots.begin());
    }
    if (v.contains("plans") && v["plans"].isArray()) {
        auto const& ps = v["plans"];
        for (std::size_t i = 0; i < ps.size(); i++) {
            auto const& a = ps[i];
            if (!a.isArray() || a.size() < 9) continue;
            PlanResult p;
            p.start = intAt(a, 0);
            p.minutes = static_cast<int>(intAt(a, 1));
            p.phasesDone = static_cast<int>(intAt(a, 2));
            p.phases = static_cast<int>(intAt(a, 3));
            p.attempts = static_cast<int>(intAt(a, 4));
            p.deaths = static_cast<int>(intAt(a, 5));
            p.playSeconds = std::max(0.0, numAt(a, 6));
            p.mastered = static_cast<int>(intAt(a, 7));
            p.completions = static_cast<int>(intAt(a, 8));
            db.plans.push_back(p);
        }
        while (db.plans.size() > MAX_PLANS) db.plans.erase(db.plans.begin());
    }
    return db;
}

}  // namespace sf
