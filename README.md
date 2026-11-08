# SkillFarm PRO

Eigenstaendige Geode-Mod fuer Geometry Dash 2.2081: misst dein Spiel (Versuche, Tode nach Prozent und Spielmodus,
Spielzeit, echte Abschluesse) und trainiert schwere Stellen im Drill (Abschnitt in Schleife, Serien, anpassendes Tempo).

- `src/core/` reine Logik ohne Geode (Datenmodell, Auswertungen, Drill-Regeln), getestet mit `tests/test_core.cpp`
- `src/main.cpp` Aufzeichnung, Erkennung von Noclip/Speedhack, Drill im Spiel
- `src/Ui.cpp` Waermekarte, Drill-Anzeige, Fenster

Gebaut wird per GitHub Actions; die fertige `.geode`-Datei liegt im Artefakt "Build Output".
