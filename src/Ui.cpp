// SkillFarm PRO – Oberflaeche: Waermekarte am Fortschrittsbalken, Drill-Anzeige, Fenster im Pause- und Hauptmenue

#include "Shared.hpp"

#include <Geode/ui/Notification.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/utils/cocos.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace sf {

namespace {

// ---------------- kleine Helfer ----------------
std::string dec(double v, int digits) {
    auto s = fmt::format("{:.{}f}", v, digits);
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

std::string speedText(double v) { return dec(v, 2) + "x"; }

std::string timeText(double sec) {
    long s = static_cast<long>(std::max(0.0, sec));
    long h = s / 3600;
    long m = (s % 3600) / 60;
    if (h > 0) return fmt::format("{} h {:02} min", h, m);
    if (m > 0) return fmt::format("{} min", m);
    return fmt::format("{} s", s);
}

int sectionLength() {
    return static_cast<int>(std::clamp<int64_t>(Mod::get()->getSettingValue<int64_t>("section-length"), 3, 30));
}

int leadIn() {
    return static_cast<int>(std::clamp<int64_t>(Mod::get()->getSettingValue<int64_t>("lead-in"), 0, 10));
}

ccColor3B heatColor(double v) {   // 0..1: gelb -> rot
    v = std::clamp(v, 0.0, 1.0);
    return {255, static_cast<GLubyte>(220.0 - 190.0 * v), static_cast<GLubyte>(40.0 * (1.0 - v))};
}

CCLabelBMFont* addLabel(CCNode* parent, std::string const& text, CCPoint pos, float scale,
                        char const* font = "chatFont.fnt", CCPoint anchor = {0.f, 0.5f}, float maxWidth = 390.f) {
    auto l = CCLabelBMFont::create(text.c_str(), font);
    l->setAnchorPoint(anchor);
    l->setPosition(pos);
    limitNodeWidth(l, maxWidth, scale, scale * 0.5f);   // lange Zeilen verkleinern statt ueber den Rand
    parent->addChild(l);
    return l;
}

CCMenuItemSpriteExtra* addButton(CCMenu* menu, std::string const& text, CCPoint pos, float scale, char const* bg,
                                 geode::Function<void(CCMenuItemSpriteExtra*)> callback) {
    auto spr = ButtonSprite::create(text.c_str(), "bigFont.fnt", bg, .8f);
    spr->setScale(scale);
    auto btn = CCMenuItemExt::createSpriteExtra(spr, std::move(callback));
    btn->setPosition(pos);
    menu->addChild(btn);
    return btn;
}

// ---------------- Fenster fuer ein Level ----------------
constexpr float LW = 440.f;
constexpr float LH = 290.f;

class LevelPopup : public geode::Popup {
protected:
    std::string m_key;
    bool m_inLevel = false;
    int m_from = -1;
    int m_to = -1;
    CCNode* m_content = nullptr;

    bool init(std::string key, bool inLevel) {
        if (!Popup::init(LW, LH)) return false;
        m_key = std::move(key);
        m_inLevel = inLevel;
        auto const* ls = db().find(m_key);
        std::string title = ls && !ls->name.empty() ? ls->name : std::string("SkillFarm PRO");
        if (title.size() > 32) title = title.substr(0, 32);
        this->setTitle(title);

        // Vorauswahl: laufender Drill, sonst die schwerste Stelle
        auto& L = live();
        if (m_inLevel && L.drill && L.key == m_key) {
            m_from = L.secFrom;
            m_to = L.secTo;
        } else if (ls) {
            auto hs = hotspots(ls->allDeaths(), 1, sectionLength(), leadIn());
            if (!hs.empty()) {
                m_from = hs[0].from;
                m_to = hs[0].to;
            }
        }
        if (m_from < 0 && m_inLevel) {
            // noch keine Tode: Abschnitt ab der aktuellen Position vorschlagen
            if (auto pl = PlayLayer::get()) {
                int p = std::clamp(static_cast<int>(pl->getCurrentPercent()), 0, 95);
                m_from = p;
                m_to = std::min(100, p + sectionLength());
            }
        }

        m_content = CCNode::create();
        m_content->setContentSize({LW, LH});
        m_content->setAnchorPoint({0.f, 0.f});
        m_content->setPosition({0.f, 0.f});
        m_mainLayer->addChild(m_content);
        this->rebuild();
        return true;
    }

    // nicht mitten im Klick neu aufbauen (der Knopf wuerde sich selbst loeschen)
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
        bool levelLive = m_inLevel && isCurrent(PlayLayer::get()) && L.key == m_key;
        auto const* ls = db().find(m_key);
        if (!ls) {
            addLabel(m_content, "Noch keine Daten fuer dieses Level.", {LW / 2, LH / 2}, .7f, "chatFont.fnt", {.5f, .5f});
            return;
        }

        // --- Waermekarte: wo du stirbst ---
        float hx = 25.f, hy = 196.f, hw = LW - 50.f, hh = 50.f;
        float bw = hw / BINS;
        auto box = CCLayerColor::create({0, 0, 0, 90}, hw, hh);
        box->setPosition({hx, hy});
        m_content->addChild(box);
        auto deaths = ls->allDeaths();
        int maxD = *std::max_element(deaths.begin(), deaths.end());
        if (ls->platformer) {
            addLabel(m_content, "Platformer-Level: keine Prozent-Karte", {hx + hw / 2, hy + hh / 2}, .6f, "chatFont.fnt", {.5f, .5f});
        } else if (maxD <= 0) {
            addLabel(m_content, "Noch keine Tode aufgezeichnet", {hx + hw / 2, hy + hh / 2}, .6f, "chatFont.fnt", {.5f, .5f});
        } else {
            for (int i = 0; i < BINS; i++) {
                if (deaths[i] <= 0) continue;
                double v = static_cast<double>(deaths[i]) / maxD;
                auto c = heatColor(v);
                float h = std::max(2.f, static_cast<float>(hh * v));
                auto bar = CCLayerColor::create({c.r, c.g, c.b, 235}, std::max(bw - .4f, .5f), h);
                bar->setPosition({hx + i * bw, hy});
                m_content->addChild(bar);
            }
        }
        if (m_from >= 0 && m_to > m_from && !ls->platformer) {
            auto sel = CCLayerColor::create({255, 255, 255, 45}, (m_to - m_from) * bw, hh);
            sel->setPosition({hx + m_from * bw, hy});
            m_content->addChild(sel);
        }
        addLabel(m_content, "0%", {hx, hy - 7.f}, .3f, "bigFont.fnt", {0.f, .5f});
        addLabel(m_content, "50%", {hx + hw / 2, hy - 7.f}, .3f, "bigFont.fnt", {.5f, .5f});
        addLabel(m_content, "100%", {hx + hw, hy - 7.f}, .3f, "bigFont.fnt", {1.f, .5f});

        // --- Zahlen ---
        float y = 172.f;
        addLabel(m_content,
                 fmt::format("Versuche: {} normal, {} Uebung, {} Startpos   -   Spielzeit: {}", ls->attempts[0],
                             ls->attempts[1], ls->attempts[2], timeText(ls->playTime)),
                 {25.f, y}, .55f);
        y -= 14.f;
        std::string self = ls->completedLegit ? "ja (gemessen)" : (ls->manualCompleted ? "ja (von Hand)" : "nein");
        addLabel(m_content,
                 fmt::format("Bester Lauf: {}%   -   Uebung: {}%   -   Selbst geschafft: {}",
                             static_cast<int>(ls->best[0]), static_cast<int>(ls->best[1]), self),
                 {25.f, y}, .55f);
        y -= 14.f;
        auto rates = modeRates(ls->modeDeaths, ls->modeTime, 0.5);
        std::string rateText = "Tode pro Minute: ";
        if (rates.empty()) {
            rateText += "noch zu wenig Spielzeit";
        } else {
            for (std::size_t i = 0; i < rates.size() && i < 4; i++) {
                if (i) rateText += "  -  ";
                rateText += fmt::format("{} {}", modeName(rates[i].mode), dec(rates[i].perMinute, 1));
            }
        }
        addLabel(m_content, rateText, {25.f, y}, .55f);
        if (ls->flaggedRuns > 0) {
            y -= 14.f;
            auto l = addLabel(m_content, fmt::format("Nicht gezaehlt (Noclip/Speedhack): {} Versuche", ls->flaggedRuns),
                              {25.f, y}, .55f);
            l->setColor({255, 150, 120});
        }

        // --- schwerste Stellen ---
        float hsY = 112.f;
        addLabel(m_content, "Schwerste Stellen:", {25.f, hsY}, .55f);
        auto hs = ls->platformer ? std::vector<Hotspot>{} : hotspots(deaths, 3, sectionLength(), leadIn());
        if (hs.empty()) {
            addLabel(m_content, "noch keine", {130.f, hsY}, .55f);
        }
        float bx = 165.f;
        for (auto const& h : hs) {
            addButton(menu, fmt::format("{}-{}% ({})", h.from, h.to, h.deaths), {bx, hsY}, .55f, "GJ_button_04.png",
                      [this, h](CCMenuItemSpriteExtra*) {
                          m_from = h.from;
                          m_to = h.to;
                          this->later();
                      });
            bx += 92.f;
        }

        // --- Drill ---
        if (!ls->platformer && levelLive && m_from >= 0 && m_to > m_from) {
            float dy = 82.f;
            addLabel(m_content, fmt::format("Abschnitt: {}% bis {}%", m_from, m_to), {25.f, dy}, .6f);
            addButton(menu, "-", {190.f, dy}, .55f, "GJ_button_05.png", [this](CCMenuItemSpriteExtra*) {
                m_from = std::max(0, m_from - 1);
                this->later();
            });
            addButton(menu, "+", {218.f, dy}, .55f, "GJ_button_05.png", [this](CCMenuItemSpriteExtra*) {
                m_from = std::min(m_to - 1, m_from + 1);
                this->later();
            });
            addLabel(m_content, "Start", {232.f, dy}, .5f);
            addButton(menu, "-", {290.f, dy}, .55f, "GJ_button_05.png", [this](CCMenuItemSpriteExtra*) {
                m_to = std::max(m_from + 1, m_to - 1);
                this->later();
            });
            addButton(menu, "+", {318.f, dy}, .55f, "GJ_button_05.png", [this](CCMenuItemSpriteExtra*) {
                m_to = std::min(100, m_to + 1);
                this->later();
            });
            addLabel(m_content, "Ende", {332.f, dy}, .5f);

            auto cfg = drillConfig();
            std::string status;
            auto it = db().levels.find(m_key);
            Section const* sec = nullptr;
            if (it != db().levels.end()) {
                for (auto const& s : it->second.sections) {
                    if (s.from == m_from && s.to == m_to) sec = &s;
                }
            }
            if (sec && sec->tries > 0) {
                double rate = recentRate(*sec);
                status = fmt::format("Tempo {}   -   Serie {}/{}   -   Quote {}   -   {} von {} geschafft", speedText(sec->speed),
                                     sec->streak, drillGoal(*sec, cfg), rate < 0 ? std::string("-") : fmt::format("{}%", std::lround(rate * 100)),
                                     sec->success, sec->tries);
                if (drillMastered(*sec, cfg)) status += "   -   GEMEISTERT";
            } else {
                status = fmt::format("Neu: Start mit {}, Ziel {}. {} in Folge = schneller, beim Ziel {} in Folge = gemeistert.",
                                     speedText(cfg.startSpeed), speedText(cfg.targetSpeed), cfg.streakGoal, cfg.masteryGoal);
            }
            addLabel(m_content, status, {25.f, dy - 20.f}, .5f);

            bool running = L.drill && L.secFrom == m_from && L.secTo == m_to;
            addButton(menu, running ? "Drill stoppen" : "Drill starten", {LW / 2 - 80.f, 30.f}, .7f,
                      running ? "GJ_button_06.png" : "GJ_button_01.png", [this, running](CCMenuItemSpriteExtra*) {
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
            addLabel(m_content, m_inLevel ? "Drill: nur fuer das laufende Level" : "Drill: im Level ueber das Pausemenue starten",
                     {25.f, 82.f}, .55f);
        }

        // --- von Hand als selbst geschafft markieren (z. B. fuer Level von vor der Mod) ---
        addButton(menu, ls->manualCompleted ? "Selbst geschafft: ja" : "Selbst geschafft: nein", {LW / 2 + 95.f, 30.f}, .55f,
                  "GJ_button_04.png", [this](CCMenuItemSpriteExtra*) {
                      auto& l = db().level(m_key);
                      l.manualCompleted = !l.manualCompleted;
                      saveDb();
                      this->later();
                  });
    }

public:
    static LevelPopup* create(std::string key, bool inLevel) {
        auto ret = new LevelPopup();
        if (ret->init(std::move(key), inLevel)) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

// ---------------- Gesamtstatistik ----------------
constexpr float GW = 420.f;
constexpr float GH = 270.f;

class GlobalPopup : public geode::Popup {
protected:
    bool init() {
        if (!Popup::init(GW, GH)) return false;
        this->setTitle("SkillFarm PRO");

        auto& d = db();
        double play = 0;
        long attempts = 0, deaths = 0, flagged = 0;
        std::array<int, MODES> md{};
        std::array<double, MODES> mt{};
        std::vector<std::string> selfDone;
        int demons = 0;
        for (auto const& [key, l] : d.levels) {
            play += l.playTime;
            attempts += l.totalAttempts();
            deaths += l.totalDeaths();
            flagged += l.flaggedRuns;
            for (int i = 0; i < MODES; i++) {
                md[i] += l.modeDeaths[i];
                mt[i] += l.modeTime[i];
            }
            if (l.selfCompleted()) {
                selfDone.push_back(l.name.empty() ? key : l.name);
                if (l.demonDifficulty >= 0) demons++;
            }
        }
        double week = 0;
        auto now = unixNow();
        for (auto const& s : d.sessions) {
            if (s.start >= now - 7 * 86400) week += s.seconds;
        }

        float y = GH - 52.f;
        auto line = [&](std::string const& text, float scale = .6f) {
            addLabel(m_mainLayer, text, {22.f, y}, scale);
            y -= 16.f;
        };
        line(fmt::format("Spielzeit: {}   (letzte 7 Tage: {})", timeText(play), timeText(week)));
        line(fmt::format("Sitzungen: {}   -   Versuche: {}   -   Tode: {}", d.sessions.size(), attempts, deaths));
        std::string done = fmt::format("Selbst geschafft: {} Level, davon {} Demons", selfDone.size(), demons);
        line(done);
        if (!selfDone.empty()) {
            std::string names;
            for (std::size_t i = 0; i < selfDone.size() && i < 6; i++) {
                if (i) names += ", ";
                names += selfDone[i];
            }
            if (selfDone.size() > 6) names += fmt::format(" (+{})", selfDone.size() - 6);
            line("   " + names, .5f);
        }
        y -= 4.f;
        line("Tode pro Minute je Spielmodus:");
        auto rates = modeRates(md, mt, 1.0);
        if (rates.empty()) {
            line("   noch zu wenig Spielzeit (mind. 1 Minute pro Modus)", .5f);
        } else {
            float col = 0;
            float rowY = y;
            for (std::size_t i = 0; i < rates.size(); i++) {
                auto const& r = rates[i];
                addLabel(m_mainLayer,
                         fmt::format("{}: {} / min  ({} min)", modeName(r.mode), dec(r.perMinute, 1), static_cast<long>(r.minutes)),
                         {34.f + col, rowY}, .5f);
                if (i % 2 == 1) {
                    rowY -= 14.f;
                    col = 0;
                } else {
                    col = 190.f;
                }
            }
            y = rowY - (rates.size() % 2 == 1 ? 14.f : 0.f) - 4.f;
        }
        if (flagged > 0) line(fmt::format("Nicht gezaehlt (Noclip/Speedhack): {} Versuche", flagged), .5f);
        auto acc = accountSetting();
        addLabel(m_mainLayer, acc.empty() ? "Aufzeichnung: alle Accounts" : "Aufzeichnung nur fuer: " + acc, {GW / 2, 18.f}, .45f,
                 "chatFont.fnt", {.5f, .5f});
        return true;
    }

public:
    static GlobalPopup* create() {
        auto ret = new GlobalPopup();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }
};

CCSprite* statsSprite(float scale) {
    auto spr = CCSprite::createWithSpriteFrameName("GJ_statsBtn_001.png");
    if (spr) spr->setScale(scale);
    return spr;
}

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

// ---------------- Drill-Anzeige oben ----------------
void updateHud(PlayLayer* pl) {
    if (!pl) return;
    auto& L = live();
    std::string text;
    if (L.recording && L.drill && !L.platformer) {
        if (!pl->m_isPracticeMode) {
            text = "DRILL: nur im Uebungsmodus";
        } else if (!L.drillCp) {
            text = fmt::format("DRILL {}-{}%:  Anlauf bis {}%", L.secFrom, L.secTo, L.secFrom);
        } else if (auto s = drillSection()) {
            auto cfg = drillConfig();
            double rate = recentRate(*s);
            text = fmt::format("DRILL {}-{}%    Serie {}/{}    Tempo {}    Quote {}", L.secFrom, L.secTo, s->streak,
                               drillGoal(*s, cfg), speedText(s->speed),
                               rate < 0 ? std::string("-") : fmt::format("{}%", std::lround(rate * 100)));
            if (drillMastered(*s, cfg)) text += "    GEMEISTERT";
        }
    }
    auto hud = static_cast<CCLabelBMFont*>(pl->getChildByID("drill-hud"_spr));
    if (text.empty()) {
        if (hud) hud->setVisible(false);
        L.hudText.clear();
        return;
    }
    if (!hud) {
        hud = CCLabelBMFont::create("", "bigFont.fnt");
        hud->setID("drill-hud"_spr);
        hud->setScale(.4f);
        hud->setAnchorPoint({.5f, 1.f});
        auto win = CCDirector::get()->getWinSize();
        hud->setPosition({win.width / 2, win.height - 26.f});
        pl->addChild(hud, 1001);
        L.hudText.clear();
    }
    hud->setVisible(true);
    if (text != L.hudText) {
        hud->setString(text.c_str());
        L.hudText = text;
    }
}

// ---------------- Knoepfe ----------------
void addPauseButton(CCLayer* pause) {
    auto spr = statsSprite(.75f);
    if (!spr) return;
    auto win = CCDirector::get()->getWinSize();
    auto menu = CCMenu::create();
    menu->setID("pause-menu"_spr);
    menu->setPosition({0.f, 0.f});
    auto btn = CCMenuItemExt::createSpriteExtra(spr, [](CCMenuItemSpriteExtra*) {
        auto pl = PlayLayer::get();
        auto& L = live();
        if (!pl) return;
        if (!L.recording || L.layer != pl) {
            auto acc = accountSetting();
            std::string text = acc.empty()
                ? std::string("Fuer dieses Level gibt es keine Aufzeichnung.")
                : fmt::format("SkillFarm zeichnet nur fuer den Account <cy>{}</c> auf. Melde dich damit an oder aendere die Einstellung.", acc);
            FLAlertLayer::create("SkillFarm PRO", text, "OK")->show();
            return;
        }
        if (auto popup = LevelPopup::create(L.key, true)) popup->show();
    });
    btn->setID("open-button"_spr);
    btn->setPosition({34.f, win.height / 2.f - 20.f});
    menu->addChild(btn);
    pause->addChild(menu, 20);
}

void addMenuButton(CCLayer* layer) {
    auto spr = statsSprite(.8f);
    if (!spr) return;
    auto btn = CCMenuItemExt::createSpriteExtra(spr, [](CCMenuItemSpriteExtra*) {
        if (auto popup = GlobalPopup::create()) popup->show();
    });
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
