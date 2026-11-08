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

// Zahlenliste lesen (zu kurze Listen werden mit 0 aufgefuellt, zu lange abgeschnitten)
template <class T, std::size_t N>
void getArray(matjson::Value const& v, char const* key, std::array<T, N>& out) {
    out.fill(T{});
    if (!v.contains(key) || !v[key].isArray()) return;
    auto const& arr = v[key];
    std::size_t n = std::min(arr.size(), N);
    for (std::size_t i = 0; i < n; i++) {
        if constexpr (std::is_floating_point_v<T>) {
            double d = arr[i].asDouble().unwrapOr(0.0);
            out[i] = static_cast<T>(std::isfinite(d) ? d : 0.0);
        } else {
            out[i] = static_cast<T>(arr[i].asInt().unwrapOr(0));
        }
    }
}

template <class T, std::size_t N>
matjson::Value putArray(std::array<T, N> const& a) {
    auto arr = matjson::Value::array();
    for (auto const& x : a) {
        if constexpr (std::is_floating_point_v<T>) {
            arr.push(matjson::Value(std::round(static_cast<double>(x) * 10.0) / 10.0));
        } else {
            arr.push(matjson::Value(static_cast<int64_t>(x)));
        }
    }
    return arr;
}

double round2(double x) { return std::round(x * 100.0) / 100.0; }
double round1(double x) { return std::round(x * 10.0) / 10.0; }

matjson::Value sectionToJson(Section const& s) {
    auto o = matjson::Value::object();
    o.set("f", matjson::Value(static_cast<int64_t>(s.from)));
    o.set("t", matjson::Value(static_cast<int64_t>(s.to)));
    o.set("sp", matjson::Value(round2(s.speed)));
    o.set("st", matjson::Value(static_cast<int64_t>(s.streak)));
    o.set("tr", matjson::Value(static_cast<int64_t>(s.tries)));
    o.set("su", matjson::Value(static_cast<int64_t>(s.success)));
    std::string rec;
    for (bool b : s.recent) rec += b ? '1' : '0';
    o.set("rec", matjson::Value(rec));
    o.set("ms", matjson::Value(round2(s.masteredSpeed)));
    o.set("msAt", matjson::Value(static_cast<int64_t>(s.masteredAt)));
    return o;
}

Section sectionFromJson(matjson::Value const& o) {
    Section s;
    s.from = static_cast<int>(std::clamp<int64_t>(getInt(o, "f"), 0, 100));
    s.to = static_cast<int>(std::clamp<int64_t>(getInt(o, "t"), 0, 100));
    s.speed = getNum(o, "sp");
    s.streak = static_cast<int>(std::max<int64_t>(0, getInt(o, "st")));
    s.tries = static_cast<int>(std::max<int64_t>(0, getInt(o, "tr")));
    s.success = static_cast<int>(std::max<int64_t>(0, getInt(o, "su")));
    for (char c : getStr(o, "rec")) {
        if (c == '0' || c == '1') s.recent.push_back(c == '1');
    }
    while (s.recent.size() > RECENT) s.recent.pop_front();
    s.masteredSpeed = getNum(o, "ms");
    s.masteredAt = getInt(o, "msAt");
    return s;
}

matjson::Value levelToJson(LevelStats const& l) {
    auto o = matjson::Value::object();
    o.set("name", matjson::Value(l.name));
    o.set("id", matjson::Value(static_cast<int64_t>(l.levelID)));
    o.set("stars", matjson::Value(static_cast<int64_t>(l.stars)));
    o.set("demon", matjson::Value(static_cast<int64_t>(l.demonDifficulty)));
    o.set("plat", matjson::Value(l.platformer));
    o.set("att", putArray(l.attempts));
    o.set("best", putArray(l.best));
    o.set("dn", putArray(l.deaths[0]));
    o.set("dp", putArray(l.deaths[1]));
    o.set("md", putArray(l.modeDeaths));
    o.set("mt", putArray(l.modeTime));
    o.set("time", matjson::Value(round1(l.playTime)));
    o.set("flag", matjson::Value(static_cast<int64_t>(l.flaggedRuns)));
    o.set("done", matjson::Value(l.completedLegit));
    o.set("doneAt", matjson::Value(static_cast<int64_t>(l.completedAt)));
    o.set("doneAtt", matjson::Value(static_cast<int64_t>(l.attemptsAtCompletion)));
    o.set("manual", matjson::Value(l.manualCompleted));
    auto secs = matjson::Value::array();
    for (auto const& s : l.sections) secs.push(sectionToJson(s));
    o.set("sec", secs);
    auto runs = matjson::Value::array();
    for (auto const& r : l.runs) {
        auto a = matjson::Value::array();
        a.push(matjson::Value(static_cast<int64_t>(r.time)));
        a.push(matjson::Value(round1(r.from)));
        a.push(matjson::Value(round1(r.to)));
        a.push(matjson::Value(static_cast<int64_t>(static_cast<int>(r.kind))));
        a.push(matjson::Value(static_cast<int64_t>((r.died ? 1 : 0) | (r.done ? 2 : 0))));
        runs.push(a);
    }
    o.set("runs", runs);
    o.set("last", matjson::Value(static_cast<int64_t>(l.lastPlayed)));
    return o;
}

LevelStats levelFromJson(matjson::Value const& o) {
    LevelStats l;
    l.name = getStr(o, "name");
    l.levelID = static_cast<int>(getInt(o, "id"));
    l.stars = static_cast<int>(getInt(o, "stars"));
    l.demonDifficulty = static_cast<int>(getInt(o, "demon", -1));
    l.platformer = getBool(o, "plat");
    getArray(o, "att", l.attempts);
    getArray(o, "best", l.best);
    getArray(o, "dn", l.deaths[0]);
    getArray(o, "dp", l.deaths[1]);
    getArray(o, "md", l.modeDeaths);
    getArray(o, "mt", l.modeTime);
    l.playTime = getNum(o, "time");
    l.flaggedRuns = static_cast<int>(getInt(o, "flag"));
    l.completedLegit = getBool(o, "done");
    l.completedAt = getInt(o, "doneAt");
    l.attemptsAtCompletion = static_cast<int>(getInt(o, "doneAtt"));
    l.manualCompleted = getBool(o, "manual");
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
            r.time = static_cast<int64_t>(a[0].asInt().unwrapOr(0));
            r.from = static_cast<float>(a[1].asDouble().unwrapOr(0.0));
            r.to = static_cast<float>(a[2].asDouble().unwrapOr(0.0));
            int k = static_cast<int>(a[3].asInt().unwrapOr(0));
            r.kind = static_cast<RunKind>(std::clamp(k, 0, KINDS - 1));
            int f = static_cast<int>(a[4].asInt().unwrapOr(0));
            r.died = (f & 1) != 0;
            r.done = (f & 2) != 0;
            l.runs.push_back(r);
        }
        while (l.runs.size() > MAX_RUNS) l.runs.pop_front();
    }
    l.lastPlayed = getInt(o, "last");
    return l;
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

LevelStats const* Database::find(std::string const& key) const {
    auto it = levels.find(key);
    return it == levels.end() ? nullptr : &it->second;
}

Session& Database::session(int64_t now, int64_t lastActivity, int64_t gapSec) {
    if (sessions.empty() || lastActivity <= 0 || now - lastActivity > gapSec) {
        Session s;
        s.start = now;
        sessions.push_back(s);
        while (sessions.size() > MAX_SESSIONS) sessions.erase(sessions.begin());
    }
    return sessions.back();
}

matjson::Value Database::toJson() const {
    auto root = matjson::Value::object();
    root.set("v", matjson::Value(static_cast<int64_t>(1)));
    auto lv = matjson::Value::object();
    for (auto const& [key, l] : levels) lv.set(key, levelToJson(l));
    root.set("levels", lv);
    auto ss = matjson::Value::array();
    for (auto const& s : sessions) {
        auto a = matjson::Value::array();
        a.push(matjson::Value(static_cast<int64_t>(s.start)));
        a.push(matjson::Value(round1(s.seconds)));
        a.push(matjson::Value(static_cast<int64_t>(s.attempts)));
        a.push(matjson::Value(static_cast<int64_t>(s.deaths)));
        ss.push(a);
    }
    root.set("sessions", ss);
    return root;
}

Database Database::fromJson(matjson::Value const& v) {
    Database db;
    if (!v.isObject()) return db;
    if (v.contains("levels") && v["levels"].isObject()) {
        for (auto const& item : v["levels"]) {
            auto key = item.getKey();
            if (!key || key->empty() || !item.isObject()) continue;
            db.levels[*key] = levelFromJson(item);
        }
    }
    if (v.contains("sessions") && v["sessions"].isArray()) {
        auto const& ss = v["sessions"];
        for (std::size_t i = 0; i < ss.size(); i++) {
            auto const& a = ss[i];
            if (!a.isArray() || a.size() < 4) continue;
            Session s;
            s.start = static_cast<int64_t>(a[0].asInt().unwrapOr(0));
            double sec = a[1].asDouble().unwrapOr(0.0);
            s.seconds = std::isfinite(sec) ? sec : 0.0;
            s.attempts = static_cast<int>(a[2].asInt().unwrapOr(0));
            s.deaths = static_cast<int>(a[3].asInt().unwrapOr(0));
            db.sessions.push_back(s);
        }
        while (db.sessions.size() > MAX_SESSIONS) db.sessions.erase(db.sessions.begin());
    }
    return db;
}

}  // namespace sf
