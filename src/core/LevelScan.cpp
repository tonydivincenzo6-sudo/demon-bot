#include "LevelScan.hpp"

#include <algorithm>
#include <cmath>

namespace sf {

namespace {

constexpr std::size_t MAX_SEGMENTS = 2000;

// kleine, robuste Zahlen-Leser (kein from_chars fuer Kommazahlen: nicht auf allen Plattformen da)
bool parseInt(std::string_view s, int64_t& out) {
    if (s.empty()) return false;
    bool neg = false;
    std::size_t i = 0;
    if (s[0] == '-' || s[0] == '+') {
        neg = s[0] == '-';
        i = 1;
    }
    if (i >= s.size()) return false;
    int64_t v = 0;
    for (; i < s.size(); i++) {
        char c = s[i];
        if (c < '0' || c > '9') return false;
        if (v > 100000000000LL) return false;
        v = v * 10 + (c - '0');
    }
    out = neg ? -v : v;
    return true;
}

bool parseNum(std::string_view s, double& out) {
    if (s.empty()) return false;
    bool neg = false;
    std::size_t i = 0;
    if (s[0] == '-' || s[0] == '+') {
        neg = s[0] == '-';
        i = 1;
    }
    double v = 0;
    bool digits = false;
    for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; i++) {
        v = v * 10 + (s[i] - '0');
        digits = true;
    }
    if (i < s.size() && s[i] == '.') {
        double f = 0.1;
        for (i++; i < s.size() && s[i] >= '0' && s[i] <= '9'; i++) {
            v += (s[i] - '0') * f;
            f *= 0.1;
            digits = true;
        }
    }
    if (!digits) return false;
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        int64_t e = 0;
        if (!parseInt(s.substr(i + 1), e)) return false;
        v *= std::pow(10.0, static_cast<double>(std::clamp<int64_t>(e, -30, 30)));
        i = s.size();
    }
    if (i != s.size() || !std::isfinite(v)) return false;
    out = neg ? -v : v;
    return true;
}

// ruft f(key, value) fuer jedes Paar "key,value,key,value,..." auf
template <class F>
void forPairs(std::string_view s, F&& f) {
    std::size_t pos = 0;
    while (pos < s.size()) {
        std::size_t c1 = s.find(',', pos);
        if (c1 == std::string_view::npos) break;
        std::size_t c2 = s.find(',', c1 + 1);
        std::string_view key = s.substr(pos, c1 - pos);
        std::string_view val = s.substr(c1 + 1, (c2 == std::string_view::npos ? s.size() : c2) - c1 - 1);
        f(key, val);
        if (c2 == std::string_view::npos) break;
        pos = c2 + 1;
    }
}

}  // namespace

int portalMode(int objectID) {
    switch (objectID) {
        case 12: return static_cast<int>(Mode::Cube);
        case 13: return static_cast<int>(Mode::Ship);
        case 47: return static_cast<int>(Mode::Ball);
        case 111: return static_cast<int>(Mode::Ufo);
        case 660: return static_cast<int>(Mode::Wave);
        case 745: return static_cast<int>(Mode::Robot);
        case 1331: return static_cast<int>(Mode::Spider);
        case 1933: return static_cast<int>(Mode::Swing);
        default: return -1;
    }
}

LevelShape buildShape(std::vector<Portal> portals, Mode startMode, float length, int objects) {
    LevelShape shape;
    shape.objects = std::max(objects, 0);
    if (!(length > 0) || !std::isfinite(length)) return shape;
    std::stable_sort(portals.begin(), portals.end(), [](Portal const& a, Portal const& b) { return a.x < b.x; });

    Mode cur = startMode;
    shape.segments.push_back({0.f, cur});
    for (auto const& p : portals) {
        if (!std::isfinite(p.x)) continue;
        if (p.x <= 0.f) {
            // Portal ganz am Anfang: bestimmt den Startmodus
            cur = p.mode;
            shape.segments.front().mode = cur;
            continue;
        }
        if (p.x >= length) break;
        if (p.mode == cur) continue;
        if (shape.segments.size() >= MAX_SEGMENTS) break;
        float pct = std::clamp(p.x / length * 100.f, 0.f, 100.f);
        if (pct - shape.segments.back().from < 0.01f) {
            shape.segments.back().mode = p.mode;   // zwei Portale direkt hintereinander: das letzte zaehlt
        } else {
            shape.segments.push_back({pct, p.mode});
        }
        cur = p.mode;
    }
    // gleiche Modi hintereinander zusammenfassen
    std::vector<Segment> merged;
    for (auto const& g : shape.segments) {
        if (!merged.empty() && merged.back().mode == g.mode) continue;
        merged.push_back(g);
    }
    shape.segments = std::move(merged);

    for (std::size_t i = 0; i < shape.segments.size(); i++) {
        float from = shape.segments[i].from;
        float to = i + 1 < shape.segments.size() ? shape.segments[i + 1].from : 100.f;
        shape.share[static_cast<int>(shape.segments[i].mode)] += std::max(0.f, to - from) / 100.f;
    }
    return shape;
}

ScanResult scanLevelString(std::string_view data) {
    ScanResult res;
    if (data.empty()) return res;
    std::size_t first = data.find(';');
    std::string_view header = data.substr(0, first == std::string_view::npos ? data.size() : first);

    Mode start = Mode::Cube;
    forPairs(header, [&](std::string_view key, std::string_view val) {
        int64_t v = 0;
        if (key == "kA2" && parseInt(val, v)) start = static_cast<Mode>(std::clamp<int64_t>(v, 0, MODES - 1));
        if (key == "kA22" && parseInt(val, v)) res.platformer = v != 0;
    });

    std::vector<Portal> portals;
    int objects = 0;
    float maxX = 0;
    std::size_t pos = first == std::string_view::npos ? data.size() : first + 1;
    while (pos < data.size()) {
        std::size_t end = data.find(';', pos);
        if (end == std::string_view::npos) end = data.size();
        std::string_view obj = data.substr(pos, end - pos);
        pos = end + 1;
        if (obj.empty()) continue;
        int64_t id = -1;
        double x = 0;
        bool hasX = false;
        forPairs(obj, [&](std::string_view key, std::string_view val) {
            if (key == "1") parseInt(val, id);
            else if (key == "2") hasX = parseNum(val, x);
        });
        if (id <= 0 || !hasX) continue;
        objects++;
        float fx = static_cast<float>(x);
        if (fx > maxX) maxX = fx;
        int m = portalMode(static_cast<int>(id));
        if (m >= 0) portals.push_back({fx, static_cast<Mode>(m)});
    }
    if (objects == 0 || maxX <= 0) return res;
    res.length = maxX;
    res.shape = buildShape(std::move(portals), start, maxX, objects);
    res.ok = true;
    return res;
}

}  // namespace sf
