#include "Engine.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace sf {

namespace {

constexpr int64_t DAY = 86400;

std::string pct(double v) { return std::to_string(static_cast<long>(std::lround(v))) + "%"; }

double median(std::vector<double> v) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    std::size_t n = v.size();
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

int64_t completionTime(LevelStats const& l) {
    if (l.completedLegit && l.completedAt > 0) return l.completedAt;
    if (l.manualCompleted && l.manualAt > 0) return l.manualAt;
    return 0;
}

bool overlaps(int a0, int a1, int b0, int b1) { return a0 < b1 && b0 < a1; }

}  // namespace

// =====================================================================
// Schwierigkeit
// =====================================================================
int difficultyTier(int stars, int demonDifficulty) {
    if (demonDifficulty >= 0) {
        switch (demonDifficulty) {
            case 3: return 6;
            case 4: return 7;
            case 0: return 8;
            case 5: return 9;
            case 6: return 10;
            default: return 8;
        }
    }
    if (stars <= 0) return -1;
    if (stars == 1) return 0;
    if (stars == 2) return 1;
    if (stars == 3) return 2;
    if (stars <= 5) return 3;
    if (stars <= 7) return 4;
    if (stars <= 9) return 5;
    return 8;   // 10 Sterne ohne Demon-Stufe: wie ein Hard Demon behandeln
}

double tierWeight(int tier) {
    static const double W[] = {0.6, 0.6, 0.75, 1.0, 1.35, 1.8, 2.5, 3.2, 4.0, 5.2, 6.8};
    if (tier < 0 || tier > 10) return 1.0;
    return W[tier];
}

char const* tierName(int tier) {
    static char const* const N[] = {"Auto", "Easy", "Normal", "Hard", "Harder", "Insane",
                                    "Easy Demon", "Medium Demon", "Hard Demon", "Insane Demon", "Extreme Demon"};
    if (tier < 0 || tier > 10) return "Unbewertet";
    return N[tier];
}

int demonIndex(int demonDifficulty) {
    switch (demonDifficulty) {
        case 3: return 0;
        case 4: return 1;
        case 0: return 2;
        case 5: return 3;
        case 6: return 4;
        default: return demonDifficulty >= 0 ? 2 : -1;
    }
}

char const* demonName(int index) {
    static char const* const N[] = {"Easy Demon", "Medium Demon", "Hard Demon", "Insane Demon", "Extreme Demon"};
    if (index < 0 || index >= DEMON_TIERS) return "Demon";
    return N[index];
}

// =====================================================================
// Skills
// =====================================================================
char const* skillName(int id) {
    if (id >= 0 && id < MODES) return modeName(static_cast<Mode>(id));
    if (id == S_CONSISTENCY) return "Konstanz";
    if (id == S_ENDURANCE) return "Ausdauer";
    return "?";
}

char const* skillTip(int id) {
    switch (id) {
        case 0: return "Sprung-Timing: Cube-Abschnitte im Drill mit 0,8x";
        case 1: return "Hoehe halten: kurze Ship-Abschnitte, ruhige Klicks";
        case 2: return "Ball-Rhythmus: Schwerkraftwechsel langsam ueben";
        case 3: return "Klickrhythmus: UFO-Stellen mit gleichmaessigen Klicks";
        case 4: return "Enge Wave-Passagen: Drill mit kurzen Abschnitten";
        case 5: return "Robot: Sprunghoehe ueber die Haltedauer steuern";
        case 6: return "Spider: Plattform-Wechsel einzeln im Drill trainieren";
        case 7: return "Swing: Richtungswechsel frueh und ruhig setzen";
        case S_CONSISTENCY: return "Serie zum Meistern erhoehen, saubere Wiederholungen";
        case S_ENDURANCE: return "Kuerzere Sessions oder Pausen nach 60 Minuten";
        default: return "";
    }
}

SkillProfile computeSkills(Database const& db) {
    SkillProfile sp;
    std::array<double, MODES> weighted{};
    std::array<double, MODES> seconds{};
    std::array<long, MODES> deaths{};
    long drillTries = 0, drillOk = 0;
    double ratioSum = 0, ratioWeight = 0;

    for (auto const& [key, l] : db.levels) {
        int tier = difficultyTier(l.stars, l.demonDifficulty);
        double w = tierWeight(tier);
        for (int m = 0; m < MODES; m++) {
            weighted[m] += l.modeTime[m] * w;
            seconds[m] += l.modeTime[m];
            deaths[m] += l.modeDeaths[m];
        }
        sp.attempts += l.totalAttempts();
        sp.deaths += l.totalDeaths();
        sp.playSeconds += l.playTime;
        sp.flagged += l.flaggedRuns;
        if (l.totalAttempts() > 0 || l.playTime > 0) sp.levelsPlayed++;
        if (l.selfCompleted()) {
            sp.selfCompleted++;
            int d = demonIndex(l.demonDifficulty);
            if (d >= 0) sp.demons[d]++;
            sp.highestTier = std::max(sp.highestTier, tier);
            int64_t t = completionTime(l);
            if (t >= sp.lastCompletedAt) {
                sp.lastCompletedAt = t;
                sp.lastCompleted = l.name.empty() ? key : l.name;
            }
        }
        // Konstanz: Drill-Quote
        for (auto const& s : l.sections) {
            if (s.tries < 5) continue;
            drillTries += s.tries;
            drillOk += s.success;
        }
        // Konstanz: wie nah deine normalen Laeufe an deinem Bestwert liegen
        std::vector<double> reach;
        for (auto it = l.runs.rbegin(); it != l.runs.rend() && reach.size() < 30; ++it) {
            if (it->kind == RunKind::Normal && it->from < 1.f) reach.push_back(it->to);
        }
        if (reach.size() >= 10) {
            double mx = *std::max_element(reach.begin(), reach.end());
            if (mx >= 5) {
                ratioSum += median(reach) / mx * static_cast<double>(reach.size());
                ratioWeight += static_cast<double>(reach.size());
            }
        }
    }

    for (int m = 0; m < MODES; m++) {
        auto& v = sp.values[m];
        v.amount = seconds[m] / 60.0;
        if (seconds[m] < 120) continue;   // mindestens 2 Minuten in diesem Modus
        double survival = weighted[m] / std::max(static_cast<double>(deaths[m]), 0.5);
        v.score = std::clamp(100.0 * (1.0 - std::exp(-survival / 25.0)), 1.0, 99.0);
    }

    {
        auto& v = sp.values[S_CONSISTENCY];
        double parts = 0, sum = 0;
        if (drillTries >= 20) {
            sum += static_cast<double>(drillOk) / static_cast<double>(drillTries);
            parts++;
        }
        if (ratioWeight > 0) {
            sum += ratioSum / ratioWeight;
            parts++;
        }
        v.amount = static_cast<double>(drillTries) + ratioWeight;
        if (parts > 0) v.score = std::clamp(100.0 * sum / parts, 1.0, 99.0);
    }

    {
        // Ausdauer: Tode pro Minute in den ersten 30 Minuten gegen nach 60 Minuten
        double earlyMin = 0, lateMin = 0;
        long earlyD = 0, lateD = 0;
        for (auto const& s : db.sessions) {
            if (s.blocks.size() < 5) continue;
            for (std::size_t i = 0; i < s.blocks.size(); i++) {
                if (i < 2) {
                    earlyMin += s.blocks[i].seconds / 60.0;
                    earlyD += s.blocks[i].deaths;
                } else if (i >= 4) {
                    lateMin += s.blocks[i].seconds / 60.0;
                    lateD += s.blocks[i].deaths;
                }
            }
        }
        auto& v = sp.values[S_ENDURANCE];
        v.amount = lateMin;
        if (earlyMin >= 20 && lateMin >= 10) {
            double early = earlyD / earlyMin;
            double late = lateD / lateMin;
            v.score = std::clamp(100.0 * (early + 0.2) / (late + 0.2), 1.0, 99.0);
        }
    }

    for (int m = 0; m < MODES; m++) {
        if (sp.values[m].score < 0) continue;
        if (sp.strongest < 0 || sp.values[m].score > sp.values[sp.strongest].score) sp.strongest = m;
        if (sp.weakest < 0 || sp.values[m].score < sp.values[sp.weakest].score) sp.weakest = m;
    }

    int top = -1;
    for (int i = 0; i < DEMON_TIERS; i++) {
        if (sp.demons[i] > 0) top = i;
    }
    if (top < 0) {
        sp.playerClass = sp.highestTier >= 5 ? "Insane Player" : "Pre-Demon Player";
    } else if (top == 0) {
        sp.playerClass = sp.demons[0] < 3 ? "Early Demon Player" : "Easy Demon Player";
    } else {
        static char const* const C[] = {"Easy Demon Player", "Medium Demon Player", "Hard Demon Player",
                                        "Insane Demon Player", "Extreme Demon Player"};
        sp.playerClass = C[top];
    }
    return sp;
}

std::array<float, SKILLS> snapshotOf(SkillProfile const& sp) {
    std::array<float, SKILLS> out{};
    for (int i = 0; i < SKILLS; i++) out[i] = static_cast<float>(sp.values[i].score);
    return out;
}

double weaknessOf(SkillProfile const& sp, Mode m) {
    int i = static_cast<int>(m);
    if (i < 0 || i >= MODES || sp.values[i].score < 0) return 0.35;
    return std::clamp((100.0 - sp.values[i].score) / 100.0, 0.0, 1.0);
}

// =====================================================================
// Farm Engine
// =====================================================================
char const* bandName(Band b) {
    switch (b) {
        case Band::TooHard: return "Zu schwer (unter 40 %)";
        case Band::Building: return "Konstanz aufbauen (40-70 %)";
        case Band::Farming: return "Skill-Farming (70-85 %)";
        case Band::Ready: return "Neue Herausforderung (ueber 85 %)";
        default: return "Noch keine Drill-Daten";
    }
}

char const* bandAdvice(Band b) {
    switch (b) {
        case Band::TooHard: return "Kuerzere Abschnitte oder eine Stufe leichter";
        case Band::Building: return "Weiter trainieren und Konstanz aufbauen";
        case Band::Farming: return "Schwierigkeit langsam erhoehen";
        case Band::Ready: return "Zeit fuer die naechste Stufe";
        default: return "Starte einen Drill, damit die Quote messbar wird";
    }
}

FarmStatus farmStatus(Database const& db, SkillProfile const& sp, int64_t now, int coachGoal) {
    FarmStatus fs;
    long ok = 0, total = 0;
    for (auto const& [key, l] : db.levels) {
        for (auto const& s : l.sections) {
            if (s.lastTried <= 0 || now - s.lastTried > 14 * DAY) continue;
            for (bool b : s.recent) {
                total++;
                ok += b ? 1 : 0;
            }
        }
    }
    fs.samples = static_cast<int>(total);
    if (total >= 10) {
        fs.rate = static_cast<double>(ok) / static_cast<double>(total);
        if (fs.rate < 0.4) fs.band = Band::TooHard;
        else if (fs.rate < 0.7) fs.band = Band::Building;
        else if (fs.rate < 0.85) fs.band = Band::Farming;
        else fs.band = Band::Ready;
    }
    auto coach = coachInfo(sp, coachGoal);
    fs.coachReady = coach.ready;
    if (coach.current >= 0) fs.baseTier = 6 + coach.current;
    else fs.baseTier = sp.highestTier >= 0 ? std::max(sp.highestTier, 5) : 5;
    int target = fs.baseTier + (coach.ready && coach.current < 4 ? 1 : 0);
    if (fs.band == Band::TooHard) target -= 1;
    if (fs.band == Band::Ready) target += 1;
    fs.targetTier = std::clamp(target, 1, 10);
    return fs;
}

std::vector<LevelPick> recommendLevels(Database const& db, SkillProfile const& sp, FarmStatus const& fs, std::size_t count) {
    struct Cand {
        std::string name;
        int levelID;
        bool main;
        int tier;
        LevelShape const* shape;
        bool started;
    };
    std::map<std::string, Cand> cands;
    for (auto const& [key, info] : db.library) {
        if (info.platformer || !info.shape.known()) continue;
        cands[key] = {info.name, info.levelID, info.main, difficultyTier(info.stars, info.demonDifficulty), &info.shape, false};
    }
    for (auto const& [key, l] : db.levels) {
        if (l.selfCompleted()) {
            cands.erase(key);
            continue;
        }
        if (l.platformer || !l.shape.known()) {
            auto it = cands.find(key);
            if (it != cands.end()) it->second.started = l.totalAttempts() > 0;
            continue;
        }
        cands[key] = {l.name, l.levelID, l.main, difficultyTier(l.stars, l.demonDifficulty), &l.shape, l.totalAttempts() > 0};
    }

    std::vector<LevelPick> out;
    for (auto const& [key, c] : cands) {
        if (c.tier < 0) continue;
        double match = 0;
        int focus = 0;
        double focusVal = -1;
        for (int m = 0; m < MODES; m++) {
            double w = weaknessOf(sp, static_cast<Mode>(m));
            double v = c.shape->share[m] * w;
            match += v;
            if (v > focusVal) {
                focusVal = v;
                focus = m;
            }
        }
        double dt = c.tier - fs.targetTier;
        double fit = std::exp(-(dt * dt) / 1.5);
        LevelPick p;
        p.key = key;
        p.name = c.name.empty() ? key : c.name;
        p.levelID = c.levelID;
        p.main = c.main;
        p.tier = c.tier;
        p.focus = static_cast<Mode>(focus);
        p.focusShare = c.shape->share[focus];
        p.started = c.started;
        p.score = 0.55 * fit + 0.45 * match + (c.started ? 0.08 : 0.0);
        if (weaknessOf(sp, p.focus) >= 0.4 && p.focusShare >= 0.2) {
            p.reason = std::string("viel ") + modeName(p.focus) + " (" + pct(p.focusShare * 100) + ") - deine Schwaeche";
        } else {
            p.reason = std::string("passt zu deinem Niveau (") + tierName(c.tier) + ")";
        }
        out.push_back(std::move(p));
    }
    std::stable_sort(out.begin(), out.end(), [](LevelPick const& a, LevelPick const& b) { return a.score > b.score; });
    if (out.size() > count) out.resize(count);
    return out;
}

std::vector<SectionPick> recommendSections(Database const& db, SkillProfile const& sp, FarmStatus const& fs, std::size_t count,
                                           int window, int leadIn, DrillConfig const& cfg, int64_t now) {
    std::vector<SectionPick> out;
    for (auto const& [key, l] : db.levels) {
        if (l.platformer || l.totalDeaths() < 5) continue;
        int tier = difficultyTier(l.stars, l.demonDifficulty);
        auto hs = hotspots(l.allDeaths(), 3, window, leadIn);
        for (auto const& h : hs) {
            bool mastered = false;
            for (auto const& s : l.sections) {
                if (overlaps(s.from, s.to, h.from, h.to) && drillMastered(s, cfg)) mastered = true;
            }
            if (mastered) continue;
            SectionPick p;
            p.key = key;
            p.name = l.name.empty() ? key : l.name;
            p.levelID = l.levelID;
            p.main = l.main;
            p.from = h.from;
            p.to = h.to;
            p.deaths = h.deaths;
            if (l.shape.known()) {
                p.hasMode = true;
                p.mode = l.shape.modeAt((h.from + h.to) / 2.f);
            }
            double weak = p.hasMode ? weaknessOf(sp, p.mode) : 0.35;
            double dt = tier >= 0 ? static_cast<double>(tier - fs.targetTier) : 0.0;
            double fit = tier >= 0 ? std::exp(-(dt * dt) / 3.0) : 0.6;
            double recency = (l.lastPlayed > 0 && now - l.lastPlayed < 30 * DAY) ? 1.0 : 0.5;
            p.score = std::log(1.0 + h.deaths) * (0.6 + weak) * (0.5 + fit) * recency;
            out.push_back(std::move(p));
        }
    }
    std::stable_sort(out.begin(), out.end(), [](SectionPick const& a, SectionPick const& b) { return a.score > b.score; });
    if (out.size() > count) out.resize(count);
    return out;
}

// =====================================================================
// Demon Coach
// =====================================================================
CoachInfo coachInfo(SkillProfile const& sp, int goal) {
    CoachInfo c;
    c.demons = sp.demons;
    c.goal = std::max(goal, 1);
    for (int i = 0; i < DEMON_TIERS; i++) {
        if (c.demons[i] > 0) c.current = i;
    }
    if (c.current < 0) {
        c.next = 0;
        c.headline = "Naechstes Ziel: dein erster Easy Demon";
        c.detail = "Farme Insane-Level und trainiere ihre schwersten Stellen im Drill.";
        return c;
    }
    c.count = c.demons[c.current];
    c.ready = c.count >= c.goal;
    c.next = std::min(c.current + 1, DEMON_TIERS - 1);
    std::string tier = demonName(c.current);
    std::string next = demonName(c.next);
    if (c.current == DEMON_TIERS - 1) {
        c.headline = "Extreme Demon geschafft!";
        c.detail = "Halte dein Niveau mit Drills auf deinen schwersten Stellen.";
    } else if (c.ready) {
        c.headline = std::to_string(c.count) + "/" + std::to_string(c.goal) + " " + tier + "s geschafft - bereit fuer " + next + "s!";
        c.detail = "Nimm dir einen " + next + " vor und trainiere ihn abschnittsweise.";
    } else {
        int left = c.goal - c.count;
        c.headline = std::to_string(c.count) + "/" + std::to_string(c.goal) + " " + tier + "s geschafft";
        c.detail = "Noch " + std::to_string(left) + " " + tier + (left == 1 ? "" : "s") + ", dann " + next + "s.";
    }
    return c;
}

// =====================================================================
// Improvement Engine
// =====================================================================
std::vector<Finding> improvement(Database const& db, SkillProfile const& sp, FarmStatus const& fs, int64_t now, int tzOffset,
                                 DrillConfig const& cfg) {
    std::vector<Finding> out;

    // 1) Plateau im Projekt-Level: viele normale Versuche, aber seit 3 Spieltagen kein neuer Bestwert
    {
        std::string bestKey;
        std::size_t bestCount = 0;
        for (auto const& [key, l] : db.levels) {
            if (l.selfCompleted()) continue;
            std::size_t n = 0;
            for (auto const& r : l.runs) {
                if (r.kind == RunKind::Normal && now - r.time < 14 * DAY) n++;
            }
            if (n > bestCount) {
                bestCount = n;
                bestKey = key;
            }
        }
        if (bestCount >= 15) {
            auto const& l = db.levels.at(bestKey);
            std::set<int> days;
            for (auto const& r : l.runs) {
                if (r.kind == RunKind::Normal) days.insert(dayIndex(r.time, tzOffset));
            }
            if (days.size() >= 4) {
                auto it = days.end();
                std::advance(it, -3);
                int cutoff = *it;   // die letzten 3 Spieltage
                float before = 0, recent = 0;
                int recentRuns = 0;
                for (auto const& r : l.runs) {
                    if (r.kind != RunKind::Normal) continue;
                    if (dayIndex(r.time, tzOffset) >= cutoff) {
                        recent = std::max(recent, r.to);
                        recentRuns++;
                    } else {
                        before = std::max(before, r.to);
                    }
                }
                if (recentRuns >= 30 && recent <= before + 0.5f && before < 100.f) {
                    out.push_back({"Plateau in " + (l.name.empty() ? bestKey : l.name) + ": seit 3 Spieltagen kein neuer Bestwert (" +
                                       pct(before) + ")",
                                   "Methode wechseln: Drill auf die schwerste Stelle oder eine Stufe leichter farmen.", true});
                }
            }
        }
    }

    // 2) Derselbe Abschnitt ohne Durchbruch
    {
        for (auto const& [key, l] : db.levels) {
            for (auto const& s : l.sections) {
                if (s.tries < 40 || drillMastered(s, cfg)) continue;
                if (s.lastTried <= 0 || now - s.lastTried > 14 * DAY) continue;
                double r = recentRate(s);
                if (r < 0 || r >= 0.5) continue;
                out.push_back({"Du uebst " + (l.name.empty() ? key : l.name) + " " + std::to_string(s.from) + "-" + std::to_string(s.to) +
                                   "% schon " + std::to_string(s.tries) + "x ohne Durchbruch",
                               "Abschnitt kuerzer machen (Ende -) oder das Starttempo senken.", true});
                break;
            }
            if (out.size() >= 3) break;
        }
    }

    // 3) Insgesamt zu schwer
    if (fs.band == Band::TooHard) {
        out.push_back({"Erfolgsquote im Drill unter 40 %", "Eine Stufe leichter farmen oder kuerzere Abschnitte waehlen.", true});
    }

    // 4) Schwaeche wird kaum trainiert
    if (sp.weakest >= 0 && sp.values[sp.weakest].score < 55) {
        Mode weak = static_cast<Mode>(sp.weakest);
        bool trained = false;
        for (auto const& [key, l] : db.levels) {
            if (!l.shape.known()) continue;
            for (auto const& s : l.sections) {
                if (s.lastTried > 0 && now - s.lastTried < 7 * DAY && l.shape.modeAt((s.from + s.to) / 2.f) == weak) trained = true;
            }
        }
        if (!trained) {
            out.push_back({std::string("Deine Schwaeche (") + modeName(weak) + ") trainierst du kaum",
                           std::string("Im Farm-Tab ein Level mit viel ") + modeName(weak) + " waehlen.", true});
        }
    }

    // 5) Konstanz steigt nicht (Vergleich mit vor 14 Tagen)
    {
        int today = dayIndex(now, tzOffset);
        float nowVal = -1, thenVal = -1;
        for (auto const& [day, vals] : db.snapshots) {
            if (day <= today && vals[S_CONSISTENCY] >= 0) nowVal = vals[S_CONSISTENCY];
            if (day <= today - 14 && vals[S_CONSISTENCY] >= 0) thenVal = vals[S_CONSISTENCY];
        }
        if (nowVal >= 0 && thenVal >= 0 && nowVal <= thenVal + 1.f) {
            out.push_back({"Deine Konstanz steigt seit 2 Wochen nicht",
                           "Serie zum Meistern erhoehen (z. B. 7): trainiert saubere Wiederholungen.", true});
        }
    }

    // 6) Lange Sessions und sinkende Ausdauer
    {
        double sum = 0;
        int n = 0;
        for (auto it = db.sessions.rbegin(); it != db.sessions.rend() && n < 5; ++it, ++n) sum += it->seconds;
        auto const& end = sp.values[S_ENDURANCE];
        if (n >= 3 && sum / n > 7200 && end.score >= 0 && end.score < 70) {
            out.push_back({"Lange Sessions: nach 60 Minuten wirst du schwaecher", "Nach 60 Minuten 10 Minuten Pause einbauen.", true});
        }
    }

    // 7) Leichter Tag empfohlen
    if (recommendedPlan(db, now, tzOffset, 90) <= 45) {
        out.push_back({"Du hast 3 Tage am Stueck lange trainiert",
                       "Heute ein leichter Tag: mehr Spielzeit heisst nicht automatisch mehr Fortschritt.", true});
    }

    if (out.empty()) {
        out.push_back({"Kein Plateau erkannt", "Weiter so: dein Training wirkt.", false});
    }
    if (out.size() > 4) out.resize(4);
    return out;
}

// =====================================================================
// Session Manager
// =====================================================================
std::vector<PlanStep> makePlan(int minutes) {
    if (minutes <= 45) return {{Phase::WarmUp, 10}, {Phase::Weakness, 15}, {Phase::Farming, 15}, {Phase::Review, 5}};
    if (minutes <= 60) return {{Phase::WarmUp, 10}, {Phase::Weakness, 15}, {Phase::Farming, 15}, {Phase::Demon, 15}, {Phase::Review, 5}};
    if (minutes <= 90) return {{Phase::WarmUp, 15}, {Phase::Weakness, 20}, {Phase::Farming, 20}, {Phase::Demon, 25}, {Phase::Review, 10}};
    return {{Phase::WarmUp, 15}, {Phase::Weakness, 25}, {Phase::Farming, 30}, {Phase::Demon, 40}, {Phase::Review, 10}};
}

char const* phaseName(Phase p) {
    switch (p) {
        case Phase::WarmUp: return "Warm-up";
        case Phase::Weakness: return "Schwaechen-Training";
        case Phase::Farming: return "Skill-Farming";
        case Phase::Demon: return "Demon-Progression";
        case Phase::Review: return "Auswertung";
    }
    return "";
}

int planMinutes(std::vector<PlanStep> const& plan) {
    int m = 0;
    for (auto const& s : plan) m += s.minutes;
    return m;
}

int recommendedPlan(Database const& db, int64_t now, int tzOffset, int defaultMinutes) {
    std::map<int, double> perDay;
    for (auto const& s : db.sessions) perDay[dayIndex(s.start, tzOffset)] += s.seconds;
    int today = dayIndex(now, tzOffset);
    int longDays = 0;
    for (int d = today - 1; d >= today - 3; d--) {
        auto it = perDay.find(d);
        if (it != perDay.end() && it->second >= 3600) longDays++;
    }
    return longDays >= 3 ? 45 : defaultMinutes;
}

PhaseState phaseAt(std::vector<PlanStep> const& plan, double elapsedSec) {
    PhaseState st;
    double t = std::max(0.0, elapsedSec);
    for (std::size_t i = 0; i < plan.size(); i++) {
        double len = plan[i].minutes * 60.0;
        if (t < len) {
            st.index = static_cast<int>(i);
            st.inPhase = t;
            st.phaseLen = len;
            return st;
        }
        t -= len;
    }
    st.finished = true;
    st.index = plan.empty() ? 0 : static_cast<int>(plan.size()) - 1;
    st.phaseLen = plan.empty() ? 0 : plan.back().minutes * 60.0;
    st.inPhase = st.phaseLen;
    return st;
}

// =====================================================================
// Summen und Diagramme
// =====================================================================
Totals totals(Database const& db, DrillConfig const& cfg) {
    Totals t;
    for (auto const& [key, l] : db.levels) {
        t.attempts += l.totalAttempts();
        t.deaths += l.totalDeaths();
        t.playSeconds += l.playTime;
        if (l.selfCompleted()) t.completions++;
        for (auto const& s : l.sections) {
            if (drillMastered(s, cfg)) t.mastered++;
        }
    }
    return t;
}

int dayIndex(int64_t unixTime, int tzOffset) {
    int64_t t = unixTime + tzOffset;
    int64_t d = t / DAY;
    if (t < 0 && t % DAY != 0) d--;
    return static_cast<int>(d);
}

void civilFromDays(int z0, int& year, int& month, int& day) {
    // Howard Hinnant: Tage seit 1970-01-01 -> Datum
    int64_t z = static_cast<int64_t>(z0) + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = yoe + era * 400;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    int64_t d = doy - (153 * mp + 2) / 5 + 1;
    int64_t m = mp < 10 ? mp + 3 : mp - 9;
    year = static_cast<int>(y + (m <= 2 ? 1 : 0));
    month = static_cast<int>(m);
    day = static_cast<int>(d);
}

std::vector<double> playMinutesPerDay(Database const& db, int64_t now, int tzOffset, int days) {
    days = std::max(days, 1);
    std::vector<double> out(static_cast<std::size_t>(days), 0.0);
    int today = dayIndex(now, tzOffset);
    for (auto const& s : db.sessions) {
        int idx = days - 1 - (today - dayIndex(s.start, tzOffset));
        if (idx >= 0 && idx < days) out[static_cast<std::size_t>(idx)] += s.seconds / 60.0;
    }
    return out;
}

std::vector<double> attemptsPerWeek(Database const& db, int64_t now, int tzOffset, int weeks) {
    weeks = std::max(weeks, 1);
    std::vector<double> out(static_cast<std::size_t>(weeks), 0.0);
    int today = dayIndex(now, tzOffset);
    for (auto const& s : db.sessions) {
        int ago = today - dayIndex(s.start, tzOffset);
        if (ago < 0) continue;
        int idx = weeks - 1 - ago / 7;
        if (idx >= 0) out[static_cast<std::size_t>(idx)] += s.attempts;
    }
    return out;
}

std::vector<double> demonsPerMonth(Database const& db, int64_t now, int tzOffset, int months) {
    months = std::max(months, 1);
    std::vector<double> out(static_cast<std::size_t>(months), 0.0);
    int y, m, d;
    civilFromDays(dayIndex(now, tzOffset), y, m, d);
    int nowMonth = y * 12 + (m - 1);
    for (auto const& [key, l] : db.levels) {
        if (!l.selfCompleted() || demonIndex(l.demonDifficulty) < 0) continue;
        int64_t t = completionTime(l);
        if (t <= 0) continue;
        civilFromDays(dayIndex(t, tzOffset), y, m, d);
        int idx = months - 1 - (nowMonth - (y * 12 + (m - 1)));
        if (idx >= 0 && idx < months) out[static_cast<std::size_t>(idx)] += 1;
    }
    return out;
}

std::vector<std::pair<double, double>> attemptsVsTier(Database const& db) {
    std::vector<std::pair<double, double>> out;
    for (auto const& [key, l] : db.levels) {
        if (!l.completedLegit || l.attemptsAtCompletion <= 0) continue;
        int tier = difficultyTier(l.stars, l.demonDifficulty);
        if (tier < 0) continue;
        out.push_back({static_cast<double>(tier), static_cast<double>(l.attemptsAtCompletion)});
    }
    return out;
}

std::vector<double> skillHistory(Database const& db, int64_t now, int tzOffset, int days, int skill) {
    days = std::max(days, 1);
    std::vector<double> out(static_cast<std::size_t>(days), -1.0);
    int today = dayIndex(now, tzOffset);
    for (auto const& [day, vals] : db.snapshots) {
        int idx = days - 1 - (today - day);
        if (idx < 0 || idx >= days) continue;
        double v = -1;
        if (skill < 0) {
            double sum = 0;
            int n = 0;
            for (int m = 0; m < MODES; m++) {
                if (vals[m] >= 0) {
                    sum += vals[m];
                    n++;
                }
            }
            if (n > 0) v = sum / n;
        } else if (skill < SKILLS) {
            v = vals[skill];
        }
        out[static_cast<std::size_t>(idx)] = v;
    }
    return out;
}

}  // namespace sf
