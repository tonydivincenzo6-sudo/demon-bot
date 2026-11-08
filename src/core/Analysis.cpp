#include "Analysis.hpp"

#include <algorithm>
#include <cmath>

namespace sf {

namespace {
constexpr double EPS = 1e-6;
double round2(double x) { return std::round(x * 100.0) / 100.0; }
}  // namespace

std::vector<Hotspot> hotspots(std::array<int, BINS> const& deaths, int count, int window, int leadIn) {
    std::vector<Hotspot> out;
    window = std::clamp(window, 2, BINS);
    leadIn = std::clamp(leadIn, 0, 20);
    long total = 0;
    for (int d : deaths) total += std::max(d, 0);
    if (total <= 0 || count <= 0) return out;

    std::array<bool, BINS> used{};
    for (int k = 0; k < count; k++) {
        int bestStart = -1;
        long bestSum = 0;
        double bestOff = 1e9;
        for (int s = 0; s + window <= BINS; s++) {
            bool overlap = false;
            long sum = 0;
            double weighted = 0;
            for (int i = s; i < s + window; i++) {
                if (used[i]) {
                    overlap = true;
                    break;
                }
                int d = std::max(deaths[i], 0);
                sum += d;
                weighted += static_cast<double>(d) * (i + 0.5);
            }
            if (overlap || sum <= 0) continue;
            // bei gleich vielen Toden: das Fenster, in dem die Tode am mittigsten liegen
            double center = weighted / static_cast<double>(sum);
            double off = std::fabs(center - (s + window / 2.0));
            if (sum > bestSum || (sum == bestSum && off < bestOff - EPS)) {
                bestStart = s;
                bestSum = sum;
                bestOff = off;
            }
        }
        if (bestStart < 0) break;
        for (int i = bestStart; i < bestStart + window; i++) used[i] = true;
        Hotspot h;
        h.from = std::max(0, bestStart - leadIn);
        h.to = std::min(BINS, bestStart + window);
        h.deaths = static_cast<int>(bestSum);
        h.share = static_cast<double>(bestSum) / static_cast<double>(total);
        out.push_back(h);
    }
    return out;
}

std::vector<ModeRate> modeRates(std::array<int, MODES> const& deaths, std::array<double, MODES> const& seconds,
                                double minMinutes) {
    std::vector<ModeRate> out;
    for (int i = 0; i < MODES; i++) {
        double minutes = std::max(seconds[i], 0.0) / 60.0;
        if (minutes < minMinutes || minutes <= 0) continue;
        ModeRate r;
        r.mode = static_cast<Mode>(i);
        r.minutes = minutes;
        r.deaths = std::max(deaths[i], 0);
        r.perMinute = r.deaths / minutes;
        out.push_back(r);
    }
    std::stable_sort(out.begin(), out.end(), [](ModeRate const& a, ModeRate const& b) {
        return a.perMinute > b.perMinute;
    });
    return out;
}

DrillConfig DrillConfig::sanitized() const {
    DrillConfig c = *this;
    c.minSpeed = std::clamp(c.minSpeed, 0.3, 1.0);
    c.targetSpeed = std::clamp(c.targetSpeed, c.minSpeed, 2.0);
    c.startSpeed = std::clamp(c.startSpeed, c.minSpeed, c.targetSpeed);
    c.step = std::clamp(c.step, 0.01, 0.5);
    c.streakGoal = std::clamp(c.streakGoal, 1, 100);
    c.masteryGoal = std::clamp(c.masteryGoal, 1, 100);
    c.minTries = std::clamp<std::size_t>(c.minTries, 2, RECENT);
    c.lowRate = std::clamp(c.lowRate, 0.0, 1.0);
    return c;
}

void drillPrepare(Section& s, DrillConfig const& c) {
    if (s.speed <= 0) s.speed = c.startSpeed;
    s.speed = round2(std::clamp(s.speed, c.minSpeed, c.targetSpeed));
    if (s.streak < 0) s.streak = 0;
}

int drillGoal(Section const& s, DrillConfig const& c) {
    return s.speed >= c.targetSpeed - EPS ? c.masteryGoal : c.streakGoal;
}

bool drillMastered(Section const& s, DrillConfig const& c) {
    return s.masteredSpeed > 0 && s.masteredSpeed >= c.targetSpeed - EPS;
}

double recentRate(Section const& s) {
    if (s.recent.empty()) return -1;
    std::size_t ok = 0;
    for (bool b : s.recent) ok += b ? 1 : 0;
    return static_cast<double>(ok) / static_cast<double>(s.recent.size());
}

DrillEvent drillAttempt(Section& s, bool success, DrillConfig const& c, int64_t now) {
    s.tries++;
    if (success) s.success++;
    s.recent.push_back(success);
    while (s.recent.size() > RECENT) s.recent.pop_front();

    if (success) {
        s.streak++;
        bool atTarget = s.speed >= c.targetSpeed - EPS;
        if (s.streak < drillGoal(s, c)) return DrillEvent::None;
        s.streak = 0;
        if (atTarget) {
            if (s.masteredSpeed < s.speed - EPS) {
                s.masteredSpeed = s.speed;
                s.masteredAt = now;
                return DrillEvent::Mastered;
            }
            return DrillEvent::None;
        }
        s.speed = round2(std::min(c.targetSpeed, s.speed + c.step));
        s.recent.clear();
        return DrillEvent::SpeedUp;
    }

    s.streak = 0;
    double rate = recentRate(s);
    if (s.recent.size() >= c.minTries && rate >= 0 && rate < c.lowRate && s.speed > c.minSpeed + EPS) {
        s.speed = round2(std::max(c.minSpeed, s.speed - c.step));
        s.recent.clear();
        return DrillEvent::SpeedDown;
    }
    return DrillEvent::None;
}

}  // namespace sf
