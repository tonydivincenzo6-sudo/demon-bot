// SkillFarm PRO – Oberflaeche im Level: Waermekarte am Fortschrittsbalken, Drill- und Session-Anzeige, Level-Fenster

#include "Shared.hpp"
#include "ui/Style.hpp"

#include <Geode/ui/Notification.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/utils/cocos.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;
using namespace sf::ui;

namespace sf {

namespace {

ccColor3B heatColor(double v) {   // 0..1: gelb -> rot
    v = std::clamp(v, 0.0, 1.0);
    return {255, static_cast<GLubyte>(220.0 - 190.0 * v), static_cast<GLubyte>(40.0 * (1.0 - v))};
}

ccColor3B const WHITE3 = {255, 255, 255};
ccColor3B const GRAY3 = {150, 160, 190};

// ---------------- Fenster fuer ein Level (aus dem Pausemenue) ----------------
constexpr float LW = 440.f;
constexpr float LH = 290.f;

class LevelPopup : public geode::Popup {
protected:
    std::string m_key;
    int m_from = -1;
    int m_to = -1;
    CCNode* m_content = nullptr;

    bool init(std::string key) {
        if (!Popup::init(LW, LH)) return false;
        m_key = std::move(key);
        if (m_bgSprite) m_bgSprite->setVisible(false);
        auto bg = drawNode();
        roundRect(bg, {0, 0, LW, LH}, 12.f, rgba(0, 0, 0, 0), 3.f, withAlpha(CYAN, .15f));
        roundRect(bg, {0, 0, LW, LH}, 12.f, BG, 1.f, CYAN);
        m_mainLayer->addChild(bg, -5);
        if (m_buttonMenu) m_buttonMenu->setZOrder(100);

        auto& L = live();
        auto const* ls = db().find(m_key);
        if (L.drill && L.key == m_key) {
            m_from = L.secFrom;
            m_to = L.secTo;
        } else if (ls) {
            auto hs = hotspots(ls->allDeaths(), 1, sectionLength(), leadIn());
            if (!hs.empty()) {
                m_from = std::min(hs[0].from, 98);
                m_to = std::clamp(hs[0].to, m_from + 1, 99);
            }
        }
        if (m_from < 0) {
            if (auto pl = PlayLayer::get()) {
                int p = std::clamp(static_cast<int>(pl->getCurrentPercent()), 0, 95);
                m_from = p;
                m_to = std::min(99, p + sectionLength());
            }
        }

        m_content = CCNode::create();
        m_content->setContentSize({LW, LH});
        m_mainLayer->addChild(m_content, 1);
        this->rebuild();
        return true;
    }

    void later() {
        geode::Ref<LevelPopup> self = this;
        geode::queueInMainThread([self] { self->rebuild(); });
    }

    void rebuild() {
        m_content->removeAllChildrenWithCleanup(true);
        auto menu = CCMenu::create();
        menu->setPosition({0.f, 0.f});
        m_content->addChild(menu, 5);

        auto& L = live();
        bool levelLive = isCurrent(PlayLayer::get()) && L.key == m_key;
        auto const* ls = db().find(m_key);
        if (!ls) {
            text(m_content, "Noch keine Daten fuer dieses Level.", {LW / 2, LH / 2}, .7f, WHITE3, {.5f, .5f});
            return;
        }
        int tier = levelTier(*ls);
        heading(m_content, ls->name.empty() ? std::string("SkillFarm PRO") : ls->name, {LW / 2, LH - 16.f}, CYAN, .5f, {.5f, .5f}, 330.f);
        text(m_content, tierName(tier), {LW / 2, LH - 32.f}, .45f, GRAY3, {.5f, .5f});
        if (auto face = difficultySprite(tier, .5f)) {
            face->setPosition({LW - 34.f, LH - 26.f});
            m_content->addChild(face, 3);
        }

        // --- Waermekarte + Aufbau ---
        panel(m_content, {14.f, 178.f, LW - 28.f, 70.f}, CYAN);
        float hx = 24.f, hy = 200.f, hw = LW - 48.f, hh = 40.f;
        float bw = hw / BINS;
        auto deaths = ls->allDeaths();
        int maxD = *std::max_element(deaths.begin(), deaths.end());
        if (ls->platformer) {
            text(m_content, "Platformer-Level: keine Prozent-Karte", {hx + hw / 2, hy + hh / 2}, .55f, GRAY3, {.5f, .5f});
        } else if (maxD <= 0) {
            text(m_content, "Noch keine Tode aufgezeichnet", {hx + hw / 2, hy + hh / 2}, .55f, GRAY3, {.5f, .5f});
        } else {
            auto dn = drawNode();
            for (int i = 0; i < BINS; i++) {
                if (deaths[i] <= 0) continue;
                double v = static_cast<double>(deaths[i]) / maxD;
                auto c = heatColor(v);
                float h = std::max(2.f, static_cast<float>(hh * v));
                float x = hx + i * bw;
                CCPoint pts[4] = {{x, hy}, {x + std::max(bw - .4f, .5f), hy}, {x + std::max(bw - .4f, .5f), hy + h}, {x, hy + h}};
                dn->drawPolygon(pts, 4, rgba(c.r, c.g, c.b, 235), 0.f, rgba(0, 0, 0, 0));
            }
            m_content->addChild(dn, 2);
        }
        if (m_from >= 0 && m_to > m_from && !ls->platformer) {
            auto sel = drawNode();
            CCPoint pts[4] = {{hx + m_from * bw, hy}, {hx + m_to * bw, hy}, {hx + m_to * bw, hy + hh}, {hx + m_from * bw, hy + hh}};
            sel->drawPolygon(pts, 4, rgba(255, 255, 255, 40), .6f, rgba(255, 255, 255, 120));
            m_content->addChild(sel, 3);
        }
        if (ls->shape.known()) shareBar(m_content, {hx, 191.f, hw, 4.f}, ls->shape.share);
        text(m_content, "0%", {hx, 184.f}, .38f, GRAY3, {0.f, .5f});
        text(m_content, "50%", {hx + hw / 2, 184.f}, .38f, GRAY3, {.5f, .5f});
        text(m_content, "100%", {hx + hw, 184.f}, .38f, GRAY3, {1.f, .5f});

        // --- Zahlen ---
        float y = 164.f;
        text(m_content,
             fmt::format("Versuche: {} normal, {} Uebung, {} Startpos   -   Spielzeit: {}", ls->attempts[0], ls->attempts[1],
                         ls->attempts[2], timeText(ls->playTime)),
             {22.f, y}, .5f, WHITE3, {0.f, .5f}, LW - 44.f);
        y -= 13.f;
        std::string self = ls->completedLegit ? "ja (gemessen)" : (ls->manualCompleted ? "ja (von Hand)" : "nein");
        text(m_content,
             fmt::format("Bester Lauf: {}%   -   Uebung: {}%   -   Selbst geschafft: {}", static_cast<int>(ls->best[0]),
                         static_cast<int>(ls->best[1]), self),
             {22.f, y}, .5f, WHITE3, {0.f, .5f}, LW - 44.f);
        y -= 13.f;
        std::string shapeText;
        if (ls->shape.known()) {
            std::vector<std::pair<float, int>> parts;
            for (int m = 0; m < MODES; m++) {
                if (ls->shape.share[m] >= 0.03f) parts.push_back({ls->shape.share[m], m});
            }
            std::sort(parts.rbegin(), parts.rend());
            shapeText = "Aufbau: ";
            for (std::size_t i = 0; i < parts.size() && i < 4; i++) {
                if (i) shapeText += ", ";
                shapeText += fmt::format("{} {}%", modeName(static_cast<Mode>(parts[i].second)), std::lround(parts[i].first * 100));
            }
            shapeText += "   -   ";
        }
        auto rates = modeRates(ls->modeDeaths, ls->modeTime, 0.5);
        std::string rateText = "Tode/min: ";
        if (rates.empty()) {
            rateText += "noch zu wenig Spielzeit";
        } else {
            for (std::size_t i = 0; i < rates.size() && i < 3; i++) {
                if (i) rateText += ", ";
                rateText += fmt::format("{} {}", modeName(rates[i].mode), dec(rates[i].perMinute, 1));
            }
        }
        text(m_content, shapeText + rateText, {22.f, y}, .5f, WHITE3, {0.f, .5f}, LW - 44.f);
        if (ls->flaggedRuns > 0) {
            y -= 13.f;
            text(m_content, fmt::format("Nicht gezaehlt (Noclip/Speedhack): {} Versuche", ls->flaggedRuns), {22.f, y}, .5f,
                 to3(ORANGE), {0.f, .5f}, LW - 44.f);
        }

        // --- schwerste Stellen ---
        float hsY = 106.f;
        heading(m_content, "SCHWERSTE STELLEN", {22.f, hsY}, PURPLE, .3f);
        auto hs = ls->platformer ? std::vector<Hotspot>{} : hotspots(deaths, 3, sectionLength(), leadIn());
        if (hs.empty()) text(m_content, "noch keine", {150.f, hsY}, .5f, GRAY3);
        float bx = 186.f;
        for (auto const& h : hs) {
            std::string label = fmt::format("{}-{}% ({})", h.from, h.to, h.deaths);
            if (ls->shape.known()) label = fmt::format("{}-{}% {}", h.from, h.to, modeName(ls->shape.modeAt((h.from + h.to) / 2.f)));
            int hFrom = std::min(h.from, 98);
            int hTo = std::clamp(h.to, hFrom + 1, 99);   // Drill endet hoechstens bei 99 %
            bool sel = hFrom == m_from && hTo == m_to;
            button(menu, label, {bx, hsY}, {86.f, 17.f}, PURPLE, sel, [this, hFrom, hTo](CCMenuItemSpriteExtra*) {
                m_from = hFrom;
                m_to = hTo;
                this->later();
            });
            bx += 90.f;
        }

        // --- Drill ---
        if (!ls->platformer && levelLive && m_from >= 0 && m_to > m_from) {
            float dy = 80.f;
            heading(m_content, fmt::format("ABSCHNITT {}% - {}%", m_from, m_to), {22.f, dy}, CYAN, .32f);
            auto stepButton = [&](char const* label, float x, auto fn) {
                button(menu, label, {x, dy}, {20.f, 16.f}, CYAN, false, [this, fn](CCMenuItemSpriteExtra*) {
                    fn();
                    this->later();
                });
            };
            text(m_content, "Start", {214.f, dy}, .45f, GRAY3, {1.f, .5f});
            stepButton("-", 228.f, [this] { m_from = std::max(0, m_from - 1); });
            stepButton("+", 252.f, [this] { m_from = std::min(m_to - 1, m_from + 1); });
            text(m_content, "Ende", {306.f, dy}, .45f, GRAY3, {1.f, .5f});
            stepButton("-", 320.f, [this] { m_to = std::max(m_from + 1, m_to - 1); });
            stepButton("+", 344.f, [this] { m_to = std::min(99, m_to + 1); });

            auto cfg = drillConfig();
            std::string status;
            Section const* sec = nullptr;
            for (auto const& s : ls->sections) {
                if (s.from == m_from && s.to == m_to) sec = &s;
            }
            if (sec && sec->tries > 0) {
                double rate = recentRate(*sec);
                status = fmt::format("Tempo {}   -   Serie {}/{}   -   Quote {}   -   {} von {} geschafft", speedText(sec->speed),
                                     sec->streak, drillGoal(*sec, cfg), percentText(rate < 0 ? -1 : rate * 100), sec->success,
                                     sec->tries);
                if (drillMastered(*sec, cfg)) status += "   -   GEMEISTERT";
            } else {
                status = fmt::format("Neu: Start mit {}, Ziel {}. {} in Folge = schneller, beim Ziel {} in Folge = gemeistert.",
                                     speedText(cfg.startSpeed), speedText(cfg.targetSpeed), cfg.streakGoal, cfg.masteryGoal);
            }
            text(m_content, status, {22.f, dy - 19.f}, .48f, WHITE3, {0.f, .5f}, LW - 44.f);

            bool running = L.drill && L.secFrom == m_from && L.secTo == m_to;
            button(menu, running ? "DRILL STOPPEN" : "DRILL STARTEN", {88.f, 28.f}, {130.f, 20.f}, running ? RED : CYAN, true,
                   [this, running](CCMenuItemSpriteExtra*) {
                       auto pl = PlayLayer::get();
                       if (running) {
                           drillDisable(pl);
                       } else {
                           drillEnable(pl, m_from, m_to);
                           showToast("Drill an: weiter spielen, Anlauf bis zum Abschnitt");
                       }
                       this->later();
                   });
        } else if (!ls->platformer) {
            text(m_content, "Drill: nur fuer das laufende Level", {22.f, 80.f}, .5f, GRAY3);
        }

        button(menu, ls->manualCompleted ? "SELBST GESCHAFFT: JA" : "SELBST GESCHAFFT: NEIN", {244.f, 28.f}, {150.f, 18.f}, GOLD,
               ls->manualCompleted, [this](CCMenuItemSpriteExtra*) {
                   auto& l = db().level(m_key);
                   l.manualCompleted = !l.manualCompleted;
                   l.manualAt = l.manualCompleted ? unixNow() : 0;
                   markDirty();
                   saveDb();
                   this->later();
               });
        button(menu, "DASHBOARD", {372.f, 28.f}, {90.f, 18.f}, PURPLE, false, [](CCMenuItemSpriteExtra*) { showDashboard(0); });
    }

public:
    static LevelPopup* create(std::string key) {
        auto ret = new LevelPopup();
        if (ret->init(std::move(key))) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

}  // namespace

// ---------------- Waermekarte am Fortschrittsbalken ----------------
void buildHeatmap(PlayLayer* pl) {
    if (!pl || !pl->m_progressBar) return;   // Balken noch nicht da: spaeter erneut
    auto& L = live();
    L.heatmapReady = true;
    auto bar = pl->m_progressBar;
    if (auto old = bar->getChildByID("heatmap"_spr)) old->removeFromParent();
    if (!L.recording || L.platformer || !Mod::get()->getSettingValue<bool>("heatmap")) return;

    auto size = bar->getContentSize();
    float x0 = 2.f;
    float w = std::max(size.width - 4.f, 10.f) / BINS;
    auto node = CCNode::create();
    node->setID("heatmap"_spr);
    for (int i = 0; i < BINS; i++) {
        auto cell = CCLayerColor::create({255, 60, 0, 0}, std::max(w, .5f), 3.f);
        cell->setPosition({x0 + i * w, -4.f});
        cell->setTag(i + 1);
        node->addChild(cell);
    }
    auto mark = CCLayerColor::create({255, 255, 255, 0}, 1.f, 1.5f);
    mark->setTag(1000);
    mark->setPosition({x0, -6.f});
    node->addChild(mark);
    bar->addChild(node, 10);
    refreshHeatmap(pl);
}

void refreshHeatmap(PlayLayer* pl) {
    if (!pl || !pl->m_progressBar) return;
    auto node = pl->m_progressBar->getChildByID("heatmap"_spr);
    if (!node) return;
    auto& L = live();
    auto const* ls = db().find(L.key);
    std::array<int, BINS> deaths{};
    if (ls) deaths = ls->allDeaths();
    int maxD = *std::max_element(deaths.begin(), deaths.end());
    for (int i = 0; i < BINS; i++) {
        auto cell = static_cast<CCLayerColor*>(node->getChildByTag(i + 1));
        if (!cell) continue;
        if (maxD <= 0 || deaths[i] <= 0) {
            cell->setOpacity(0);
            continue;
        }
        double v = static_cast<double>(deaths[i]) / maxD;
        cell->setColor(heatColor(v));
        cell->setOpacity(static_cast<GLubyte>(70.0 + 185.0 * v));
    }
    // Drill-Abschnitt als weisser Strich darunter
    if (auto mark = static_cast<CCLayerColor*>(node->getChildByTag(1000))) {
        auto size = pl->m_progressBar->getContentSize();
        float w = std::max(size.width - 4.f, 10.f) / BINS;
        if (L.drill && L.secTo > L.secFrom) {
            mark->setContentSize({(L.secTo - L.secFrom) * w, 1.5f});
            mark->setPosition({2.f + L.secFrom * w, -6.f});
            mark->setOpacity(220);
        } else {
            mark->setOpacity(0);
        }
    }
}

// ---------------- Anzeigen oben: Drill (Mitte, unter dem Fortschrittsbalken), Session (links oben) ----------------
namespace {
// Label einmal anlegen, danach nur den Text tauschen (setString nur bei Aenderung: kein Ruckeln)
void setHudLabel(PlayLayer* pl, char const* id, std::string const& text, std::string& lastText, char const* font, float scale,
                 CCPoint anchor, CCPoint pos, ccColor3B color, float maxW) {
    auto label = typeinfo_cast<CCLabelBMFont*>(pl->getChildByID(id));
    if (text.empty()) {
        if (label) label->setVisible(false);
        lastText.clear();
        return;
    }
    if (!label) {
        label = CCLabelBMFont::create("", font);
        label->setID(id);
        label->setAnchorPoint(anchor);
        label->setPosition(pos);
        label->setColor(color);
        label->setOpacity(225);
        pl->addChild(label, 1001);
        lastText.clear();
    }
    label->setVisible(true);
    if (text != lastText) {
        label->setString(text.c_str());
        limitNodeWidth(label, maxW, scale, scale * .5f);
        lastText = text;
    }
}
}  // namespace

void updateHud(PlayLayer* pl) {
    if (!pl) return;
    auto& L = live();
    auto win = CCDirector::get()->getWinSize();

    std::string text;
    if (L.recording && L.drill && !L.platformer) {
        if (!pl->m_isPracticeMode) {
            text = "DRILL: NUR IM UEBUNGSMODUS";
        } else if (!L.drillCp) {
            text = fmt::format("DRILL {}-{}%: ANLAUF BIS {}%", L.secFrom, L.secTo, L.secFrom);
        } else if (auto s = drillSection()) {
            auto cfg = drillConfig();
            double rate = recentRate(*s);
            text = fmt::format("DRILL {}-{}%  SERIE {}/{}  {}  QUOTE {}", L.secFrom, L.secTo, s->streak, drillGoal(*s, cfg),
                               speedText(s->speed), percentText(rate < 0 ? -1 : rate * 100));
            if (drillMastered(*s, cfg)) text += "  GEMEISTERT";
        }
    }
    setHudLabel(pl, "drill-hud"_spr, text, L.hudText, "bigFont.fnt", .32f, {.5f, 1.f}, {win.width / 2, win.height - 24.f},
                {255, 255, 255}, std::min(300.f, win.width * .6f));

    // Session Manager: klein links oben, damit Fortschrittsbalken und Pause-Knopf frei bleiben
    std::string stext;
    auto& P = planRun();
    if (P.active && !P.steps.empty()) {
        auto st = phaseAt(P.steps, P.elapsed);
        stext = fmt::format("Session {}/{}: {} - noch {}{}", st.index + 1, P.steps.size(),
                            phaseName(P.steps[static_cast<std::size_t>(st.index)].phase),
                            clockText(std::max(0.0, st.phaseLen - st.inPhase)), P.paused ? " (Pause)" : "");
    }
    setHudLabel(pl, "session-hud"_spr, stext, L.sessionText, "chatFont.fnt", .5f, {0.f, 1.f}, {6.f, win.height - 4.f},
                {0, 214, 255}, std::min(150.f, win.width * .28f));
}

// ---------------- Knoepfe ----------------
void showLevelPopup() {
    auto pl = PlayLayer::get();
    auto& L = live();
    if (!pl) return;
    if (!L.recording || L.layer != pl) {
        auto acc = accountSetting();
        std::string msg = acc.empty()
            ? std::string("Fuer dieses Level gibt es keine Aufzeichnung.")
            : fmt::format("SkillFarm zeichnet nur fuer den Account <cy>{}</c> auf. Melde dich damit an oder aendere die Einstellung.", acc);
        FLAlertLayer::create("SkillFarm PRO", msg, "OK")->show();
        return;
    }
    if (auto popup = LevelPopup::create(L.key)) popup->show();
}

void addPauseButton(CCLayer* pause) {
    auto win = CCDirector::get()->getWinSize();
    auto menu = CCMenu::create();
    menu->setID("pause-menu"_spr);
    menu->setPosition({0.f, 0.f});
    auto btn = CCMenuItemExt::createSpriteExtra(skillfarmIcon(32.f), [](CCMenuItemSpriteExtra*) { showLevelPopup(); });
    btn->setID("open-button"_spr);
    btn->setPosition({34.f, win.height / 2.f - 20.f});
    menu->addChild(btn);
    pause->addChild(menu, 20);
}

void addMenuButton(CCLayer* layer) {
    auto btn = CCMenuItemExt::createSpriteExtra(skillfarmIcon(38.f), [](CCMenuItemSpriteExtra*) { showDashboard(0); });
    btn->setID("stats-button"_spr);
    if (auto side = typeinfo_cast<CCMenu*>(layer->getChildByID("right-side-menu"))) {
        side->addChild(btn);
        side->updateLayout();
        return;
    }
    auto win = CCDirector::get()->getWinSize();
    auto menu = CCMenu::create();
    menu->setID("menu"_spr);
    menu->setPosition({0.f, 0.f});
    btn->setPosition({win.width - 30.f, win.height * .42f});
    menu->addChild(btn);
    layer->addChild(menu, 20);
}

void showToast(std::string const& text) {
    Notification::create(text, NotificationIcon::Info, 2.5f)->show();
}

void playSound(char const* file) {
    if (!Mod::get()->getSettingValue<bool>("sounds")) return;
    if (auto fm = FMODAudioEngine::sharedEngine()) fm->playEffect(file);
}

}  // namespace sf
