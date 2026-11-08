// SkillFarm PRO – Aufbau eines Levels aus seinen Portalen lesen (ohne Geode, getestet)
#pragma once

#include "Model.hpp"

#include <string_view>
#include <vector>

namespace sf {

// Spielmodus eines Portals, -1 wenn das Objekt kein Modus-Portal ist
int portalMode(int objectID);

struct Portal {
    float x = 0;
    Mode mode = Mode::Cube;
};

// Aufbau aus Portal-Positionen: Anteil je Modus (nach Strecke) und Abschnitte
LevelShape buildShape(std::vector<Portal> portals, Mode startMode, float length, int objects);

struct ScanResult {
    bool ok = false;
    bool platformer = false;
    float length = 0;
    LevelShape shape;
};

// Entpackter GD-Level-String: "kS38,...,kA2,1,...;1,1,2,15,3,15;1,12,2,300,3,15;..."
ScanResult scanLevelString(std::string_view data);

}  // namespace sf
