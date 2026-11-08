// SkillFarm PRO – Zeichen-Helfer im Neon-Look: Panels, Ringe, Diagramme, Knoepfe
#pragma once

#include "../core/Model.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/cocos.hpp>

#include <string>
#include <utility>
#include <vector>

namespace sf::ui {

using cocos2d::CCDrawNode;
using cocos2d::CCLabelBMFont;
using cocos2d::CCMenu;
using cocos2d::CCNode;
using cocos2d::CCPoint;
using cocos2d::CCRect;
using cocos2d::CCSize;
using cocos2d::CCSprite;
using cocos2d::ccColor3B;
using cocos2d::ccColor4F;

inline ccColor4F rgba(int r, int g, int b, int a = 255) {
    return {r / 255.f, g / 255.f, b / 255.f, a / 255.f};
}
inline ccColor3B rgb(int r, int g, int b) {
    return {static_cast<GLubyte>(r), static_cast<GLubyte>(g), static_cast<GLubyte>(b)};
}
inline ccColor3B to3(ccColor4F c) {
    return rgb(static_cast<int>(c.r * 255), static_cast<int>(c.g * 255), static_cast<int>(c.b * 255));
}
inline ccColor4F withAlpha(ccColor4F c, float a) {
    c.a = a;
    return c;
}

// Farben
inline ccColor4F const BG = rgba(9, 12, 30, 250);
inline ccColor4F const PANEL = rgba(15, 21, 46, 235);
inline ccColor4F const CYAN = rgba(0, 214, 255);
inline ccColor4F const PURPLE = rgba(168, 92, 255);
inline ccColor4F const GOLD = rgba(255, 204, 64);
inline ccColor4F const RED = rgba(255, 82, 92);
inline ccColor4F const ORANGE = rgba(255, 150, 60);
inline ccColor4F const GREEN = rgba(70, 235, 150);
inline ccColor4F const GRAY = rgba(120, 130, 160);
inline ccColor4F const DIM = rgba(40, 48, 84);

ccColor4F modeColor(Mode m);
ccColor4F scoreColor(double score);   // rot -> orange -> lila -> cyan

// Formen
void roundRect(CCDrawNode* dn, CCRect r, float radius, ccColor4F fill, float border, ccColor4F borderColor);
CCDrawNode* panel(CCNode* parent, CCRect r, ccColor4F accent, int z = 0);   // Neon-Panel mit Glow
void ring(CCNode* parent, CCPoint center, float radius, float thick, double value01, ccColor4F color);
void meter(CCNode* parent, CCRect r, double value01, ccColor4F color);
void shareBar(CCNode* parent, CCRect r, std::array<float, MODES> const& share);   // Anteil je Modus

// Text
CCLabelBMFont* heading(CCNode* parent, std::string const& t, CCPoint pos, ccColor4F color, float scale = .4f,
                       CCPoint anchor = {0.f, .5f}, float maxW = 0);
CCLabelBMFont* text(CCNode* parent, std::string const& t, CCPoint pos, float scale = .5f, ccColor3B color = {255, 255, 255},
                    CCPoint anchor = {0.f, .5f}, float maxW = 0, char const* font = "chatFont.fnt");

// Diagramme (Werte < 0 werden bei Linien ausgelassen)
void lineChart(CCNode* parent, CCRect area, std::vector<double> const& v, ccColor4F color, double maxY);
void barChart(CCNode* parent, CCRect area, std::vector<double> const& v, ccColor4F color, double maxY);
void chartFrame(CCNode* parent, CCRect area, std::string const& maxLabel);

// Knopf im Neon-Stil
CCMenuItemSpriteExtra* button(CCMenu* menu, std::string const& label, CCPoint pos, CCSize size, ccColor4F accent, bool filled,
                              geode::Function<void(CCMenuItemSpriteExtra*)> callback);

// Sprites aus GD (nullptr, wenn das Bild fehlt)
CCSprite* safeSprite(char const* frame);
CCSprite* difficultySprite(int tier, float scale);

// Zahlen
std::string dec(double v, int digits);
std::string timeText(double sec);
std::string clockText(double sec);   // 05:12
std::string speedText(double v);
std::string percentText(double v);   // "73%" oder "-" bei < 0

}  // namespace sf::ui
