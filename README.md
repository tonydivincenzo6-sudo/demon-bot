# SkillFarm PRO

Eigenstaendige Geode-Mod fuer Geometry Dash 2.2081: ein Trainings-Coach. Misst dein Spiel (Versuche, Tode nach Prozent
und Spielmodus, Spielzeit, echte Abschluesse), bewertet deine Skills, empfiehlt Level und Abschnitte, plant
Trainings-Sessions und trainiert schwere Stellen im Drill (Abschnitt in Schleife, Serien, anpassendes Tempo).
Hat nichts mit anderen Mods zu tun und spielt nie selbst.

- `src/core/` reine Logik ohne Geode (Datenmodell, Level-Analyse, Skills, Farm Engine, Coach, Improvement Engine,
  Session-Plaene, Statistik), getestet mit `tests/test_core.cpp`
- `src/main.cpp` Aufzeichnung, Erkennung von Noclip/Speedhack, Drill, Session-Uhr, Analyse der Level-Bibliothek
- `src/Ui.cpp` Waermekarte, Anzeigen im Level, Level-Fenster, Knoepfe
- `src/ui/` Dashboard im Neon-Look und Zeichen-Helfer

Gebaut wird per GitHub Actions; die fertige `.geode`-Datei liegt im Artefakt "Build Output".
