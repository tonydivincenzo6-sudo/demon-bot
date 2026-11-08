#include "Style.hpp"

#include <Geode/utils/cocos.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace sf::ui {

namespace {
constexpr float PI = 3.14159265f;
}

ccColor4F modeColor(Mode m) {
    switch (m) {
        case Mode::Cube: return rgba(80, 200, 255);
        case Mode::Ship: return rgba(255, 110, 200);
        case Mode::Ball: return rgba(255, 160, 60);
        case Mode::Ufo: return rgba(255, 230, 80);
        case Mode::Wave: return rgba(60, 255, 160);
        case Mode::Robot: return rgba(200, 200, 225);
        case Mode::Spider: return rgba(180, 110, 255);
        case Mode::Swing: return rgba(255, 90, 90);
        default: return CYAN;
    }
}

ccColor4F scoreColor(double s) {
    if (s < 0) return GRAY;
    if (s < 40) return RED;
    if (s < 60) return ORANGE;
    if (s < 75) return PURPLE;
    return CYAN;
}

// ---------------- Formen ----------------
void roundRect(CCDrawNode* dn, CCRect r, float radius, ccColor4F fill, float border, ccColor4F borderColor) {
    radius = std::max(0.f, std::min(radius, std::min(r.size.width, r.size.height) / 2.f));
    constexpr int SEG = 6;
    std::vector<CCPoint> pts;
    pts.reserve(4 * (SEG + 1));
    float x0 = r.getMinX(), x1 = r.getMaxX(), y0 = r.getMinY(), y1 = r.getMaxY();
    struct Corner {
        float cx, cy, a0;
    };
    Corner corners[4] = {{x1 - radius, y0 + radius, -PI / 2},
                         {x1 - radius, y1 - radius, 0.f},
                         {x0 + radius, y1 - radius, PI / 2},
                         {x0 + radius, y0 + radius, PI}};
    for (auto const& c : corners) {
        for (int i = 0; i <= SEG; i++) {
            float a = c.a0 + (PI / 2) * i / SEG;
            pts.push_back({c.cx + std::cos(a) * radius, c.cy + std::sin(a) * radius});
        }
    }
    dn->drawPolygon(pts.data(), static_cast<unsigned int>(pts.size()), fill, border, borderColor);
}

CCDrawNode* panel(CCNode* parent, CCRect r, ccColor4F accent, int z) {
    auto dn = CCDrawNode::create();
    // Glow: zwei weiche Rahmen, dann der Hauptrahmen
    roundRect(dn, r, 9.f, rgba(0, 0, 0, 0), 3.2f, withAlpha(accent, .12f));
    roundRect(dn, r, 9.f, rgba(0, 0, 0, 0), 1.8f, withAlpha(accent, .28f));
    roundRect(dn, r, 9.f, PANEL, .8f, withAlpha(accent, .95f));
    parent->addChild(dn, z);
    return dn;
}

void ring(CCNode* parent, CCPoint c, float radius, float thick, double value01, ccColor4F color) {
    auto dn = CCDrawNode::create();
    constexpr int SEG = 48;
    float half = thick / 2.f;
    // Hintergrund-Ring
    for (int i = 0; i < SEG; i++) {
        float a0 = 2 * PI * i / SEG, a1 = 2 * PI * (i + 1) / SEG;
        dn->drawSegment({c.x + std::cos(a0) * radius, c.y + std::sin(a0) * radius},
                        {c.x + std::cos(a1) * radius, c.y + std::sin(a1) * radius}, half, DIM);
    }
    // Wert: von oben im Uhrzeigersinn
    if (value01 > 0) {
        int n = std::max(1, static_cast<int>(std::round(std::clamp(value01, 0.0, 1.0) * SEG)));
        for (int i = 0; i < n; i++) {
            float a0 = PI / 2 - 2 * PI * i / SEG, a1 = PI / 2 - 2 * PI * (i + 1) / SEG;
            dn->drawSegment({c.x + std::cos(a0) * radius, c.y + std::sin(a0) * radius},
                            {c.x + std::cos(a1) * radius, c.y + std::sin(a1) * radius}, half, color);
        }
    }
    parent->addChild(dn);
}

void meter(CCNode* parent, CCRect r, double value01, ccColor4F color) {
    auto dn = CCDrawNode::create();
    roundRect(dn, r, r.size.height / 2, DIM, 0.f, DIM);
    double v = std::clamp(value01, 0.0, 1.0);
    if (v > 0.001) {
        CCRect f = r;
        f.size.width = std::max(r.size.height, static_cast<float>(r.size.width * v));
        roundRect(dn, f, r.size.height / 2, color, 0.f, color);
    }
    parent->addChild(dn);
}

void shareBar(CCNode* parent, CCRect r, std::array<float, MODES> const& share) {
    auto dn = CCDrawNode::create();
    float x = r.getMinX();
    for (int m = 0; m < MODES; m++) {
        float w = r.size.width * std::clamp(share[m], 0.f, 1.f);
        if (w < 0.3f) continue;
        CCPoint pts[4] = {{x, r.getMinY()}, {x + w, r.getMinY()}, {x + w, r.getMaxY()}, {x, r.getMaxY()}};
        dn->drawPolygon(pts, 4, modeColor(static_cast<Mode>(m)), 0.f, rgba(0, 0, 0, 0));
        x += w;
    }
    parent->addChild(dn);
}

// ---------------- Text ----------------
CCLabelBMFont* heading(CCNode* parent, std::string const& t, CCPoint pos, ccColor4F color, float scale, CCPoint anchor, float maxW) {
    auto l = CCLabelBMFont::create(t.c_str(), "bigFont.fnt");
    l->setAnchorPoint(anchor);
    l->setPosition(pos);
    l->setColor(to3(color));
    if (maxW > 0) limitNodeWidth(l, maxW, scale, scale * .4f);
    else l->setScale(scale);
    parent->addChild(l, 2);
    return l;
}

CCLabelBMFont* text(CCNode* parent, std::string const& t, CCPoint pos, float scale, ccColor3B color, CCPoint anchor, float maxW,
                    char const* font) {
    auto l = CCLabelBMFont::create(t.c_str(), font);
    l->setAnchorPoint(anchor);
    l->setPosition(pos);
    l->setColor(color);
    if (maxW > 0) limitNodeWidth(l, maxW, scale, scale * .45f);
    else l->setScale(scale);
    parent->addChild(l, 2);
    return l;
}

// ---------------- Diagramme ----------------
void chartFrame(CCNode* parent, CCRect a, std::string const& maxLabel) {
    auto dn = CCDrawNode::create();
    for (int i = 0; i <= 3; i++) {
        float y = a.getMinY() + a.size.height * i / 3.f;
        dn->drawSegment({a.getMinX(), y}, {a.getMaxX(), y}, .25f, withAlpha(GRAY, i == 0 ? .7f : .25f));
    }
    parent->addChild(dn);
    if (!maxLabel.empty()) text(parent, maxLabel, {a.getMinX() - 2.f, a.getMaxY()}, .35f, rgb(150, 160, 190), {1.f, .5f});
}

void lineChart(CCNode* parent, CCRect a, std::vector<double> const& v, ccColor4F color, double maxY) {
    if (v.empty()) return;
    if (!(maxY > 0)) maxY = 1;
    auto dn = CCDrawNode::create();
    auto pt = [&](std::size_t i, double y) {
        float x = v.size() == 1 ? a.getMidX() : a.getMinX() + a.size.width * static_cast<float>(i) / static_cast<float>(v.size() - 1);
        return CCPoint{x, a.getMinY() + a.size.height * static_cast<float>(std::clamp(y / maxY, 0.0, 1.0))};
    };
    bool havePrev = false;
    CCPoint prev;
    for (std::size_t i = 0; i < v.size(); i++) {
        if (v[i] < 0) {
            havePrev = false;
            continue;
        }
        CCPoint p = pt(i, v[i]);
        if (havePrev) dn->drawSegment(prev, p, .9f, color);
        dn->drawDot(p, 1.6f, color);
        prev = p;
        havePrev = true;
    }
    parent->addChild(dn, 1);
}

void barChart(CCNode* parent, CCRect a, std::vector<double> const& v, ccColor4F color, double maxY) {
    if (v.empty()) return;
    if (!(maxY > 0)) maxY = 1;
    auto dn = CCDrawNode::create();
    float slot = a.size.width / static_cast<float>(v.size());
    float bw = std::max(1.f, slot * .68f);
    for (std::size_t i = 0; i < v.size(); i++) {
        if (!(v[i] > 0)) continue;
        float h = std::max(1.f, a.size.height * static_cast<float>(std::clamp(v[i] / maxY, 0.0, 1.0)));
        float x = a.getMinX() + slot * i + (slot - bw) / 2;
        CCPoint pts[4] = {{x, a.getMinY()}, {x + bw, a.getMinY()}, {x + bw, a.getMinY() + h}, {x, a.getMinY() + h}};
        dn->drawPolygon(pts, 4, color, 0.f, rgba(0, 0, 0, 0));
    }
    parent->addChild(dn, 1);
}

// ---------------- Knopf ----------------
CCMenuItemSpriteExtra* button(CCMenu* menu, std::string const& label, CCPoint pos, CCSize size, ccColor4F accent, bool filled,
                              geode::Function<void(CCMenuItemSpriteExtra*)> callback) {
    auto node = CCNode::create();
    node->setContentSize(size);
    node->setAnchorPoint({.5f, .5f});
    auto dn = CCDrawNode::create();
    roundRect(dn, {0, 0, size.width, size.height}, std::min(6.f, size.height / 2), filled ? withAlpha(accent, .85f) : PANEL, .8f,
              accent);
    node->addChild(dn);
    auto l = CCLabelBMFont::create(label.c_str(), "bigFont.fnt");
    l->setPosition({size.width / 2, size.height / 2 + .5f});
    limitNodeSize(l, {size.width - 8.f, size.height - 4.f}, .4f, .15f);
    l->setColor(filled ? rgb(10, 14, 30) : to3(accent));
    node->addChild(l);
    auto btn = CCMenuItemExt::createSpriteExtra(node, std::move(callback));
    btn->setPosition(pos);
    menu->addChild(btn);
    return btn;
}

// ---------------- Sprites ----------------
CCSprite* safeSprite(char const* frame) {
    if (!CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName(frame)) return nullptr;
    return CCSprite::createWithSpriteFrameName(frame);
}

CCSprite* difficultySprite(int tier, float scale) {
    char const* frame = "difficulty_00_btn_001.png";
    switch (tier) {
        case 0: frame = "difficulty_auto_btn_001.png"; break;
        case 1: frame = "difficulty_01_btn_001.png"; break;
        case 2: frame = "difficulty_02_btn_001.png"; break;
        case 3: frame = "difficulty_03_btn_001.png"; break;
        case 4: frame = "difficulty_04_btn_001.png"; break;
        case 5: frame = "difficulty_05_btn_001.png"; break;
        case 6: frame = "difficulty_07_btn_001.png"; break;
        case 7: frame = "difficulty_08_btn_001.png"; break;
        case 8: frame = "difficulty_06_btn_001.png"; break;
        case 9: frame = "difficulty_09_btn_001.png"; break;
        case 10: frame = "difficulty_10_btn_001.png"; break;
        default: break;
    }
    auto s = safeSprite(frame);
    if (s) s->setScale(scale);
    return s;
}

// ---------------- Zahlen ----------------
std::string dec(double v, int digits) {
    auto s = fmt::format("{:.{}f}", v, digits);
    std::replace(s.begin(), s.end(), '.', ',');
    return s;
}

std::string timeText(double sec) {
    long s = static_cast<long>(std::max(0.0, sec));
    long h = s / 3600;
    long m = (s % 3600) / 60;
    if (h > 0) return fmt::format("{} h {:02} min", h, m);
    if (m > 0) return fmt::format("{} min", m);
    return fmt::format("{} s", s);
}

std::string clockText(double sec) {
    long s = static_cast<long>(std::max(0.0, sec));
    return fmt::format("{:02}:{:02}", s / 60, s % 60);
}

std::string speedText(double v) { return dec(v, 2) + "x"; }

std::string percentText(double v) {
    if (v < 0) return "-";
    return fmt::format("{}%", std::lround(v));
}

}  // namespace sf::ui
