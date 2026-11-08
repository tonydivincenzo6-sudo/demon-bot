// SkillFarm PRO – Dashboard im Neon-Look: Uebersicht, Skills, Farm Engine, Demon Coach, Session Manager, Statistik

#include "../Shared.hpp"
#include "Style.hpp"

#include <Geode/ui/Popup.hpp>
#include <Geode/utils/cocos.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;
using namespace sf::ui;

namespace sf {

namespace {

constexpr float W = 500.f;
constexpr float H = 300.f;
constexpr float TOP = 236.f;   // Oberkante des Inhalts

ccColor3B const WHITE3 = {255, 255, 255};
ccColor3B const GRAY3 = {150, 160, 190};

char const* const TABS[] = {"UEBERSICHT", "SKILLS", "FARM", "COACH", "SESSION", "STATISTIK"};
constexpr int TAB_COUNT = 6;

// Einstellung auf einen der vier Plaene runden
int defaultPlan() {
    int m = static_cast<int>(Mod::get()->getSettingValue<int64_t>("session-minutes"));
    return m <= 52 ? 45 : m <= 75 ? 60 : m <= 105 ? 90 : 120;
}

class Dashboard : public geode::Popup {
protected:
    int m_tab = 0;
    int m_plan = 90;
    CCNode* m_content = nullptr;
    CCNode* m_tabs = nullptr;
    double m_lastRefresh = 0;
    bool m_wasScanning = false;

    bool init(int tab) {
        if (!Popup::init(W, H)) return false;
        m_tab = std::clamp(tab, 0, TAB_COUNT - 1);
        if (m_bgSprite) m_bgSprite->setVisible(false);
        if (m_buttonMenu) m_buttonMenu->setZOrder(100);

        auto bg = CCDrawNode::create();
        roundRect(bg, {0, 0, W, H}, 14.f, rgba(0, 0, 0, 0), 4.f, withAlpha(CYAN, .12f));
        roundRect(bg, {0, 0, W, H}, 14.f, rgba(0, 0, 0, 0), 2.2f, withAlpha(CYAN, .25f));
        roundRect(bg, {0, 0, W, H}, 14.f, BG, 1.f, CYAN);
        m_mainLayer->addChild(bg, -5);

        heading(m_mainLayer, "SKILLFARM PRO", {W / 2, H - 15.f}, CYAN, .55f, {.5f, .5f});
        text(m_mainLayer, "Dein Weg zum Extreme Demon", {W / 2, H - 31.f}, .5f, rgb(205, 215, 240), {.5f, .5f});

        m_tabs = CCNode::create();
        m_mainLayer->addChild(m_tabs, 10);
        m_content = CCNode::create();
        m_content->setContentSize({W, H});
        m_mainLayer->addChild(m_content, 1);

        m_plan = recommendedPlan(db(), unixNow(), tzOffset(), defaultPlan());

        libraryScanStart();
        m_wasScanning = libraryScan().running;
        this->schedule(schedule_selector(Dashboard::tick), 0.f);
        this->rebuild();
        return true;
    }

    void later() {
        geode::Ref<Dashboard> self = this;
        geode::queueInMainThread([self] { self->rebuild(); });
    }

    void switchTab(int tab) {
        m_tab = tab;
        this->later();
    }

    // Bibliothek nebenbei analysieren, laufende Session-Uhr auffrischen
    void tick(float) {
        auto& scan = libraryScan();
        if (scan.running) libraryScanStep(.006);
        double now = wallNow();
        bool refresh = false;
        if (m_wasScanning && !scan.running) refresh = true;   // Analyse fertig
        if (scan.running && m_tab == 2 && now - m_lastRefresh > .7) refresh = true;
        if (planRun().active && (m_tab == 0 || m_tab == 4) && now - m_lastRefresh > 1.0) refresh = true;
        m_wasScanning = scan.running;
        if (refresh) this->rebuild();
    }

    void buildTabs() {
        m_tabs->removeAllChildrenWithCleanup(true);
        auto menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        m_tabs->addChild(menu);
        for (int i = 0; i < TAB_COUNT; i++) {
            button(menu, TABS[i], {50.f + i * 80.f, H - 52.f}, {76.f, 17.f}, CYAN, i == m_tab,
                   [this, i](CCMenuItemSpriteExtra*) { this->switchTab(i); });
        }
    }

    void rebuild() {
        m_lastRefresh = wallNow();
        buildTabs();
        m_content->removeAllChildrenWithCleanup(true);
        auto menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        m_content->addChild(menu, 5);

        auto& d = db();
        auto sp = computeSkills(d);
        auto fs = farmStatus(d, sp, unixNow(), coachGoal());
        switch (m_tab) {
            case 0: overview(menu, sp, fs); break;
            case 1: skills(sp); break;
            case 2: farm(menu, sp, fs); break;
            case 3: coach(menu, sp, fs); break;
            case 4: session(menu, sp, fs); break;
            default: stats(sp); break;
        }
    }

    // ---------------- 1. Uebersicht ----------------
    void overview(CCMenu* menu, SkillProfile const& sp, FarmStatus const& fs) {
        auto& d = db();
        // Profil
        panel(m_content, {10.f, 10.f, 158.f, TOP - 10.f}, CYAN);
        heading(m_content, "1. PROFIL", {18.f, TOP - 12.f}, CYAN, .36f);
        text(m_content, "Spielerprofil", {18.f, 206.f}, .42f, GRAY3);
        heading(m_content, sp.playerClass, {18.f, 192.f}, GOLD, .34f, {0.f, .5f}, 142.f);
        text(m_content, "Daten: aktiv gemessen", {18.f, 177.f}, .44f, to3(GREEN));
        text(m_content, fmt::format("Spielzeit: {}", timeText(sp.playSeconds)), {18.f, 162.f}, .46f);
        text(m_content, fmt::format("Versuche: {}", sp.attempts), {18.f, 149.f}, .46f);
        text(m_content, fmt::format("Tode: {}", sp.deaths), {18.f, 136.f}, .46f);
        int demons = 0;
        for (int x : sp.demons) demons += x;
        text(m_content, fmt::format("Selbst geschafft: {} ({} Demons)", sp.selfCompleted, demons), {18.f, 123.f}, .46f, WHITE3,
             {0.f, .5f}, 142.f);
        text(m_content, "Letzter Neuzugang:", {18.f, 108.f}, .42f, GRAY3);
        text(m_content, sp.lastCompleted.empty() ? std::string("-") : sp.lastCompleted, {18.f, 96.f}, .46f, to3(GOLD), {0.f, .5f},
             142.f);
        heading(m_content, "VERSUCHE PRO WOCHE", {18.f, 78.f}, CYAN, .26f);
        auto weeks = attemptsPerWeek(d, unixNow(), tzOffset(), 8);
        double mx = std::max(1.0, *std::max_element(weeks.begin(), weeks.end()));
        CCRect area{30.f, 20.f, 128.f, 46.f};
        chartFrame(m_content, area, fmt::format("{}", std::lround(mx)));
        lineChart(m_content, area, weeks, CYAN, mx);

        // Skills kompakt
        panel(m_content, {174.f, 10.f, 158.f, TOP - 10.f}, PURPLE);
        heading(m_content, "2. SKILL DETECTION", {182.f, TOP - 12.f}, PURPLE, .32f);
        for (int m = 0; m < MODES; m++) {
            float cx = 196.f + (m % 4) * 38.f;
            float cy = m < 4 ? 186.f : 130.f;
            auto const& v = sp.values[m];
            ring(m_content, {cx, cy}, 12.f, 4.f, v.score < 0 ? 0 : v.score / 100.0, scoreColor(v.score));
            text(m_content, percentText(v.score), {cx, cy}, .3f, WHITE3, {.5f, .5f}, 0, "bigFont.fnt");
            text(m_content, modeName(static_cast<Mode>(m)), {cx, cy - 21.f}, .4f, GRAY3, {.5f, .5f});
        }
        if (sp.strongest >= 0) {
            text(m_content, "Hoechste Faehigkeit:", {182.f, 86.f}, .42f, GRAY3);
            text(m_content, fmt::format("{} ({})", skillName(sp.strongest), percentText(sp.values[sp.strongest].score)),
                 {182.f, 74.f}, .5f, to3(GOLD));
            text(m_content, "Schwaeche:", {182.f, 58.f}, .42f, GRAY3);
            text(m_content, fmt::format("{} ({})", skillName(sp.weakest), percentText(sp.values[sp.weakest].score)), {182.f, 46.f},
                 .5f, to3(RED));
            text(m_content, skillTip(sp.weakest), {182.f, 30.f}, .4f, GRAY3, {0.f, .5f}, 144.f);
        } else {
            text(m_content, "Spiel ein paar Minuten,", {253.f, 70.f}, .45f, GRAY3, {.5f, .5f});
            text(m_content, "dann erscheinen deine Werte.", {253.f, 57.f}, .45f, GRAY3, {.5f, .5f});
        }

        // Heute
        panel(m_content, {338.f, 10.f, 152.f, TOP - 10.f}, GOLD);
        heading(m_content, "HEUTE", {346.f, TOP - 12.f}, GOLD, .36f);
        auto& P = planRun();
        if (P.active && !P.steps.empty()) {
            auto st = phaseAt(P.steps, P.elapsed);
            text(m_content, fmt::format("Session laeuft: Phase {}/{}", st.index + 1, P.steps.size()), {346.f, 204.f}, .44f,
                 to3(CYAN));
            heading(m_content, phaseName(P.steps[static_cast<std::size_t>(st.index)].phase), {346.f, 190.f}, CYAN, .3f, {0.f, .5f},
                    136.f);
            text(m_content, fmt::format("{} / {}{}", clockText(st.inPhase), clockText(st.phaseLen), P.paused ? " (Pause)" : ""),
                 {346.f, 176.f}, .46f);
        } else {
            int rec = recommendedPlan(db(), unixNow(), tzOffset(), defaultPlan());
            text(m_content, "Empfohlene Session:", {346.f, 204.f}, .42f, GRAY3);
            heading(m_content, fmt::format("{} MIN{}", rec, rec <= 45 ? " (LEICHT)" : ""), {346.f, 190.f}, GOLD, .34f, {0.f, .5f},
                    136.f);
        }
        auto c = coachInfo(sp, coachGoal());
        text(m_content, c.headline, {346.f, 158.f}, .42f, WHITE3, {0.f, .5f}, 136.f);
        text(m_content, "Naechstes Training:", {346.f, 138.f}, .42f, GRAY3);
        auto secs = recommendSections(db(), sp, fs, 1, sectionLength(), leadIn(), drillConfig(), unixNow());
        if (!secs.empty()) {
            auto const& s = secs[0];
            text(m_content, s.name, {346.f, 125.f}, .46f, to3(GOLD), {0.f, .5f}, 136.f);
            text(m_content, fmt::format("{}-{}%{}  ({} Tode)", s.from, s.to, s.hasMode ? std::string(" ") + modeName(s.mode) : "", s.deaths),
                 {346.f, 112.f}, .44f, WHITE3, {0.f, .5f}, 136.f);
        } else {
            text(m_content, "Spiel ein Level, dann zeigt", {346.f, 125.f}, .42f, GRAY3);
            text(m_content, "der Coach deine Stellen.", {346.f, 112.f}, .42f, GRAY3);
        }
        text(m_content, fmt::format("Status: {}", bandName(fs.band)), {346.f, 92.f}, .4f, GRAY3, {0.f, .5f}, 136.f);
        button(menu, "ZUM TRAINING", {414.f, 52.f}, {132.f, 18.f}, GOLD, true, [this](CCMenuItemSpriteExtra*) { this->switchTab(2); });
        button(menu, "SESSION", {414.f, 28.f}, {132.f, 18.f}, CYAN, false, [this](CCMenuItemSpriteExtra*) { this->switchTab(4); });
    }

    // ---------------- 2. Skills ----------------
    void skills(SkillProfile const& sp) {
        panel(m_content, {10.f, 10.f, 480.f, TOP - 10.f}, PURPLE);
        heading(m_content, "2. SKILL DETECTION", {18.f, TOP - 12.f}, PURPLE, .36f);
        for (int i = 0; i < SKILLS; i++) {
            float cx = 58.f + (i % 5) * 96.f;
            float cy = i < 5 ? 182.f : 106.f;
            auto const& v = sp.values[i];
            ring(m_content, {cx, cy}, 21.f, 6.f, v.score < 0 ? 0 : v.score / 100.0, scoreColor(v.score));
            text(m_content, percentText(v.score), {cx, cy}, .38f, WHITE3, {.5f, .5f}, 0, "bigFont.fnt");
            text(m_content, skillName(i), {cx, cy - 32.f}, .5f, WHITE3, {.5f, .5f});
            std::string amount;
            if (i < MODES) amount = v.amount >= 1 ? fmt::format("{} min", std::lround(v.amount)) : "keine Daten";
            else if (i == S_CONSISTENCY) amount = v.amount >= 1 ? fmt::format("{} Proben", std::lround(v.amount)) : "keine Daten";
            else amount = v.amount >= 1 ? fmt::format("{} min ab 60 min", std::lround(v.amount)) : "lange Sessions fehlen";
            text(m_content, amount, {cx, cy - 43.f}, .38f, GRAY3, {.5f, .5f});
        }
        if (sp.strongest >= 0) {
            text(m_content, fmt::format("Hoechste Faehigkeit: {} ({})", skillName(sp.strongest), percentText(sp.values[sp.strongest].score)),
                 {20.f, 42.f}, .5f, to3(GOLD));
            text(m_content, fmt::format("Schwaeche: {} ({}) - {}", skillName(sp.weakest), percentText(sp.values[sp.weakest].score),
                                        skillTip(sp.weakest)),
                 {20.f, 29.f}, .5f, to3(RED), {0.f, .5f}, 460.f);
        }
        text(m_content, "Aus deinen echten Daten, nach Level-Schwierigkeit gewichtet. '-' = zu wenig Daten (mind. 2 min pro Modus).",
             {20.f, 16.f}, .38f, GRAY3, {0.f, .5f}, 460.f);
    }

    // ---------------- 3. Farm Engine ----------------
    void farm(CCMenu* menu, SkillProfile const& sp, FarmStatus const& fs) {
        bool lvl = inLevel();
        panel(m_content, {10.f, 196.f, 480.f, 40.f}, CYAN);
        text(m_content, fmt::format("AKTUELLER STATUS: {}", bandName(fs.band)), {18.f, 225.f}, .5f, to3(GOLD), {0.f, .5f}, 300.f);
        std::string quote = fs.rate >= 0 ? fmt::format("Quote {} aus {} Durchgaengen. ", percentText(fs.rate * 100), fs.samples) : "";
        text(m_content, quote + bandAdvice(fs.band), {18.f, 209.f}, .42f, GRAY3, {0.f, .5f}, 300.f);
        heading(m_content, "DYNAMISCHE SCHWIERIGKEIT", {330.f, 227.f}, CYAN, .24f);
        meter(m_content, {330.f, 211.f, 152.f, 6.f}, fs.targetTier / 10.0, PURPLE);
        text(m_content, fmt::format("Ziel: {}", tierName(fs.targetTier)), {406.f, 202.f}, .4f, WHITE3, {.5f, .5f});

        // Level
        panel(m_content, {10.f, 10.f, 236.f, 180.f}, CYAN);
        heading(m_content, "3. EMPFOHLENE LEVEL", {18.f, 182.f}, CYAN, .3f);
        auto& scan = libraryScan();
        if (scan.running) {
            text(m_content, fmt::format("Analyse {}/{}", scan.done, scan.total), {238.f, 182.f}, .38f, to3(GOLD), {1.f, .5f});
        }
        auto picks = recommendLevels(db(), sp, fs, 4);
        if (picks.empty()) {
            text(m_content, scan.running ? "Deine Level werden analysiert ..." : "Noch keine passenden Level gefunden.", {128.f, 120.f},
                 .46f, GRAY3, {.5f, .5f}, 220.f);
            text(m_content, "Spiel ein paar Level oder suche online.", {128.f, 106.f}, .42f, GRAY3, {.5f, .5f}, 220.f);
        }
        float y = 158.f;
        for (auto const& p : picks) {
            if (auto face = difficultySprite(p.tier, .42f)) {
                face->setPosition({30.f, y});
                m_content->addChild(face, 3);
            }
            heading(m_content, p.name, {48.f, y + 7.f}, rgba(235, 240, 255), .28f, {0.f, .5f}, 140.f);
            text(m_content, p.reason, {48.f, y - 7.f}, .4f, to3(modeColor(p.focus)), {0.f, .5f}, 140.f);
            if (!lvl) {
                auto key = p.key;
                int id = p.levelID;
                bool main = p.main;
                button(menu, "OEFFNEN", {214.f, y}, {50.f, 16.f}, CYAN, false,
                       [key, id, main](CCMenuItemSpriteExtra*) { openLevel(key, id, main); });
            }
            y -= 32.f;
        }
        if (!lvl) {
            button(menu, fmt::format("ONLINE SUCHEN: {}", tierName(fs.targetTier)), {128.f, 22.f}, {210.f, 16.f}, GOLD, false,
                   [tier = fs.targetTier](CCMenuItemSpriteExtra*) { openSearch(tier); });
        } else {
            text(m_content, "Level oeffnen geht nur ausserhalb eines Levels.", {128.f, 22.f}, .4f, GRAY3, {.5f, .5f}, 220.f);
        }

        // Abschnitte
        panel(m_content, {254.f, 10.f, 236.f, 180.f}, PURPLE);
        heading(m_content, "ABSCHNITTE ZUM TRAINIEREN", {262.f, 182.f}, PURPLE, .3f);
        auto secs = recommendSections(db(), sp, fs, 4, sectionLength(), leadIn(), drillConfig(), unixNow());
        if (secs.empty()) {
            text(m_content, "Noch keine Tode aufgezeichnet.", {372.f, 120.f}, .46f, GRAY3, {.5f, .5f}, 220.f);
            text(m_content, "Spiel ein Level, dann erscheinen", {372.f, 106.f}, .42f, GRAY3, {.5f, .5f}, 220.f);
            text(m_content, "hier deine schwersten Stellen.", {372.f, 94.f}, .42f, GRAY3, {.5f, .5f}, 220.f);
        }
        y = 158.f;
        for (auto const& s : secs) {
            heading(m_content, s.name, {262.f, y + 7.f}, rgba(235, 240, 255), .28f, {0.f, .5f}, 150.f);
            text(m_content,
                 fmt::format("{}-{}%{}  -  {} Tode", s.from, s.to, s.hasMode ? std::string("  ") + modeName(s.mode) : "", s.deaths),
                 {262.f, y - 7.f}, .42f, s.hasMode ? to3(modeColor(s.mode)) : WHITE3, {0.f, .5f}, 150.f);
            if (!lvl) {
                auto key = s.key;
                int id = s.levelID, from = s.from, to = s.to;
                bool main = s.main;
                button(menu, "DRILL", {460.f, y}, {44.f, 16.f}, PURPLE, true, [key, id, main, from, to](CCMenuItemSpriteExtra*) {
                    requestDrill(key, from, to);
                    openLevel(key, id, main);
                });
            }
            y -= 32.f;
        }
        if (!secs.empty()) {
            text(m_content, lvl ? "Drill im Level ueber das Pausemenue." : "DRILL oeffnet das Level, der Drill startet von selbst.",
                 {372.f, 22.f}, .38f, GRAY3, {.5f, .5f}, 220.f);
        }
    }

    // ---------------- 4. Demon Coach ----------------
    void coach(CCMenu* menu, SkillProfile const& sp, FarmStatus const& fs) {
        auto c = coachInfo(sp, coachGoal());
        panel(m_content, {10.f, 112.f, 480.f, 124.f}, PURPLE);
        heading(m_content, "5. DEMON COACH & PROGRESSION", {18.f, TOP - 12.f}, PURPLE, .32f);
        static char const* const TIER_SHORT[] = {"EASY", "MEDIUM", "HARD", "INSANE", "EXTREME"};
        ccColor4F const cols[] = {CYAN, rgba(110, 150, 255), PURPLE, rgba(220, 80, 200), RED};
        auto path = CCDrawNode::create();
        float y = 172.f;
        for (int i = 0; i < DEMON_TIERS - 1; i++) {
            float x0 = 60.f + i * 95.f, x1 = 60.f + (i + 1) * 95.f;
            path->drawSegment({x0 + 18.f, y}, {x1 - 18.f, y}, 1.4f, cols[i + 1]);
            // Pfeilspitze
            CCPoint tri[3] = {{x1 - 18.f, y}, {x1 - 25.f, y + 4.f}, {x1 - 25.f, y - 4.f}};
            path->drawPolygon(tri, 3, cols[i + 1], 0.f, rgba(0, 0, 0, 0));
        }
        for (int i = 0; i < DEMON_TIERS; i++) {
            float x = 60.f + i * 95.f;
            bool current = i == c.current;
            bool target = i == (c.ready ? c.next : std::max(c.current, 0)) && !(c.current == DEMON_TIERS - 1 && c.ready);
            if (current) path->drawDot({x, y}, 21.f, withAlpha(GOLD, .35f));
            path->drawDot({x, y}, 17.f, cols[i]);
            path->drawDot({x, y}, 15.5f, PANEL);
            if (target) {
                CCPoint tri[3] = {{x, y + 24.f}, {x - 5.f, y + 31.f}, {x + 5.f, y + 31.f}};
                path->drawPolygon(tri, 3, GOLD, 0.f, rgba(0, 0, 0, 0));
            }
        }
        m_content->addChild(path, 1);
        for (int i = 0; i < DEMON_TIERS; i++) {
            float x = 60.f + i * 95.f;
            if (auto face = difficultySprite(6 + i, .5f)) {
                face->setPosition({x, y});
                m_content->addChild(face, 3);
            }
            heading(m_content, TIER_SHORT[i], {x, y - 29.f}, i == c.current ? GOLD : rgba(230, 235, 255), .27f, {.5f, .5f});
            text(m_content, fmt::format("{} geschafft", c.demons[i]), {x, y - 41.f}, .4f, GRAY3, {.5f, .5f});
        }
        text(m_content, "ZIEL", {60.f + (c.ready ? c.next : std::max(c.current, 0)) * 95.f, y + 38.f}, .3f, to3(GOLD), {.5f, .5f}, 0,
             "bigFont.fnt");

        // Naechster Schritt
        panel(m_content, {10.f, 10.f, 236.f, 96.f}, GOLD);
        heading(m_content, "NAECHSTER SCHRITT", {18.f, 96.f}, GOLD, .3f);
        text(m_content, c.headline, {18.f, 79.f}, .48f, to3(GOLD), {0.f, .5f}, 220.f);
        text(m_content, c.detail, {18.f, 64.f}, .42f, WHITE3, {0.f, .5f}, 220.f);
        text(m_content, "Langzeitziel: Extreme Demon", {18.f, 49.f}, .42f, GRAY3);
        if (!inLevel()) {
            int tier = 6 + (c.current < 0 ? 0 : c.next);
            button(menu, fmt::format("DEMONS SUCHEN: {}", tierName(tier)), {128.f, 24.f}, {210.f, 16.f}, GOLD, false,
                   [tier](CCMenuItemSpriteExtra*) { openSearch(tier); });
        }

        // Improvement Engine
        panel(m_content, {254.f, 10.f, 236.f, 96.f}, ORANGE);
        heading(m_content, "IMPROVEMENT ENGINE", {262.f, 96.f}, ORANGE, .3f);
        auto found = improvement(db(), sp, fs, unixNow(), tzOffset(), drillConfig());
        float fy = 80.f;
        for (std::size_t i = 0; i < found.size() && i < 3; i++) {
            text(m_content, found[i].title, {262.f, fy}, .42f, found[i].problem ? to3(ORANGE) : to3(GREEN), {0.f, .5f}, 220.f);
            text(m_content, found[i].tip, {262.f, fy - 11.f}, .38f, GRAY3, {0.f, .5f}, 220.f);
            fy -= 25.f;
        }
    }

    // ---------------- 5. Session Manager ----------------
    void session(CCMenu* menu, SkillProfile const&, FarmStatus const&) {
        auto& P = planRun();
        int rec = recommendedPlan(db(), unixNow(), tzOffset(), defaultPlan());

        panel(m_content, {10.f, 10.f, 176.f, TOP - 10.f}, CYAN);
        heading(m_content, "PLAN WAEHLEN", {18.f, TOP - 12.f}, CYAN, .34f);
        text(m_content, rec <= 45 ? "Heute empfohlen: leichter Tag" : fmt::format("Heute empfohlen: {} Min", rec), {18.f, 206.f}, .44f,
             to3(GOLD), {0.f, .5f}, 160.f);
        int const options[] = {45, 60, 90, 120};
        for (int i = 0; i < 4; i++) {
            int m = options[i];
            std::string label = m == 45 ? "45 MIN (LEICHT)" : fmt::format("{} MIN", m);
            bool sel = P.active ? P.minutes == m || (P.minutes > 90 && m == 120) : m_plan == m;
            button(menu, label, {98.f, 182.f - i * 23.f}, {150.f, 18.f}, CYAN, sel, [this, m](CCMenuItemSpriteExtra*) {
                if (planRun().active) return;
                m_plan = m;
                this->later();
            });
        }
        if (!P.active) {
            button(menu, "SESSION STARTEN", {98.f, 72.f}, {150.f, 22.f}, GOLD, true, [this](CCMenuItemSpriteExtra*) {
                planStart(m_plan);
                this->later();
            });
        } else {
            button(menu, P.paused ? "WEITER" : "PAUSE", {98.f, 80.f}, {150.f, 18.f}, CYAN, true, [this](CCMenuItemSpriteExtra*) {
                planTogglePause();
                this->later();
            });
            button(menu, "BEENDEN", {98.f, 57.f}, {150.f, 18.f}, RED, false, [this](CCMenuItemSpriteExtra*) {
                planStop(false);
                this->later();
            });
        }
        auto const& plans = db().plans;
        if (!plans.empty()) {
            auto const& r = plans.back();
            text(m_content, fmt::format("Letzte: {} Min, {}/{} Phasen", r.minutes, r.phasesDone, r.phases), {18.f, 32.f}, .4f, GRAY3,
                 {0.f, .5f}, 160.f);
            text(m_content, fmt::format("{} Versuche, {} gemeistert, {} geschafft", r.attempts, r.mastered, r.completions),
                 {18.f, 20.f}, .4f, GRAY3, {0.f, .5f}, 160.f);
        }

        auto steps = P.active ? P.steps : makePlan(m_plan);
        int total = planMinutes(steps);
        panel(m_content, {194.f, 10.f, 296.f, TOP - 10.f}, PURPLE);
        heading(m_content, fmt::format("7. SESSION MANAGER ({} MINUTEN)", total), {202.f, TOP - 12.f}, PURPLE, .3f, {0.f, .5f}, 280.f);
        auto st = phaseAt(steps, P.active ? P.elapsed : -1.0);
        int current = P.active ? st.index : -1;
        auto line = CCDrawNode::create();
        float top = 196.f, step = 38.f;
        float bottom = top - step * static_cast<float>(std::max<std::size_t>(steps.size(), 1) - 1);
        line->drawSegment({214.f, top}, {214.f, bottom}, .6f, DIM);
        for (std::size_t i = 0; i < steps.size(); i++) {
            float yy = top - step * i;
            bool done = P.active && static_cast<int>(i) < current;
            bool cur = static_cast<int>(i) == current;
            line->drawDot({214.f, yy}, 6.f, cur ? CYAN : (done ? PURPLE : DIM));
            line->drawDot({214.f, yy}, cur || done ? 3.f : 4.6f, cur || done ? rgba(240, 250, 255) : PANEL);
        }
        m_content->addChild(line, 1);
        for (std::size_t i = 0; i < steps.size(); i++) {
            float yy = top - step * i;
            bool done = P.active && static_cast<int>(i) < current;
            bool cur = static_cast<int>(i) == current;
            ccColor4F col = cur ? CYAN : (done ? GRAY : rgba(235, 240, 255));
            heading(m_content, fmt::format("{} MIN: {}", steps[i].minutes, phaseName(steps[i].phase)), {228.f, yy + 6.f}, col, .28f,
                    {0.f, .5f}, 190.f);
            text(m_content, planHint(steps[i].phase), {228.f, yy - 7.f}, .4f, GRAY3, {0.f, .5f}, 250.f);
            if (cur) {
                text(m_content, fmt::format("{} / {}", clockText(st.inPhase), clockText(st.phaseLen)), {482.f, yy + 6.f}, .3f, to3(GOLD),
                     {1.f, .5f}, 0, "bigFont.fnt");
            }
        }
        text(m_content, "Die Uhr laeuft auch im Menue. Phasenwechsel werden angesagt.", {342.f, 18.f}, .36f, GRAY3, {.5f, .5f}, 280.f);
    }

    // ---------------- 6. Statistik ----------------
    void stats(SkillProfile const&) {
        auto& d = db();
        int64_t now = unixNow();
        int tz = tzOffset();
        auto chartPanel = [&](CCRect r, ccColor4F accent, std::string const& title) {
            panel(m_content, r, accent);
            heading(m_content, title, {r.getMinX() + 8.f, r.getMaxY() - 10.f}, accent, .26f, {0.f, .5f}, r.size.width - 16.f);
        };
        // Spielzeit pro Tag
        chartPanel({10.f, 124.f, 236.f, 112.f}, CYAN, "SPIELZEIT PRO TAG (14 TAGE)");
        auto days = playMinutesPerDay(d, now, tz, 14);
        double mx = std::max(10.0, *std::max_element(days.begin(), days.end()));
        CCRect a1{40.f, 136.f, 196.f, 74.f};
        chartFrame(m_content, a1, fmt::format("{}m", std::lround(mx)));
        barChart(m_content, a1, days, CYAN, mx);
        // Versuche pro Woche
        chartPanel({254.f, 124.f, 236.f, 112.f}, PURPLE, "VERSUCHE PRO WOCHE (8 WOCHEN)");
        auto weeks = attemptsPerWeek(d, now, tz, 8);
        mx = std::max(10.0, *std::max_element(weeks.begin(), weeks.end()));
        CCRect a2{284.f, 136.f, 196.f, 74.f};
        chartFrame(m_content, a2, fmt::format("{}", std::lround(mx)));
        lineChart(m_content, a2, weeks, PURPLE, mx);
        // Demons pro Monat
        chartPanel({10.f, 10.f, 236.f, 108.f}, GOLD, "SELBST GESCHAFFTE DEMONS PRO MONAT");
        auto months = demonsPerMonth(d, now, tz, 12);
        mx = std::max(3.0, *std::max_element(months.begin(), months.end()));
        CCRect a3{40.f, 22.f, 196.f, 70.f};
        chartFrame(m_content, a3, fmt::format("{}", std::lround(mx)));
        barChart(m_content, a3, months, GOLD, mx);
        // Skill-Verlauf
        chartPanel({254.f, 10.f, 236.f, 108.f}, CYAN, "SKILL-VERLAUF (30 TAGE)");
        CCRect a4{284.f, 22.f, 196.f, 70.f};
        chartFrame(m_content, a4, "100");
        lineChart(m_content, a4, skillHistory(d, now, tz, 30, -1), CYAN, 100.0);
        lineChart(m_content, a4, skillHistory(d, now, tz, 30, S_CONSISTENCY), PURPLE, 100.0);
        text(m_content, "Skill", {470.f, 102.f}, .36f, to3(CYAN), {1.f, .5f});
        text(m_content, "Konstanz", {438.f, 102.f}, .36f, to3(PURPLE), {1.f, .5f});
    }

public:
    static Dashboard* create(int tab) {
        auto ret = new Dashboard();
        if (ret->init(tab)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

}  // namespace

void showDashboard(int tab) {
    takeSnapshot();   // aktueller Stand fuer den Skill-Verlauf
    if (auto d = Dashboard::create(tab)) d->show();
}

}  // namespace sf
