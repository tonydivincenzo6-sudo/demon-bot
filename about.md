# Demon Bot

Ein Knopf im Hauptmenue startet einen Autopiloten, der bewertete Classic-Level nacheinander
herunterlaedt und spielt. Beim Start waehlst du in einem Fenster jede Schwierigkeit einzeln aus
(Auto, Easy, Normal, Hard, Harder, Insane, Easy/Medium/Hard/Insane/Extreme Demon) und ob
**Classic**- und/oder **Platformer**-Level gespielt werden. Gespielt wird in dieser Reihenfolge,
erst Classic, dann Platformer.

**Platformer:** Dort reicht Noclip nicht, man muss ja selbst laufen und springen. Der Bot sucht deshalb mit dem
echten Spiel einen Weg zum Ziel (er probiert Laeufe und Spruenge aus und merkt sich, was weiter bringt) und
spielt diesen Weg danach einmal sauber von vorne. Als Fortschritt zaehlen Level-Checkpoints, eingesammelte Items
und die Naehe zum End-Trigger. Findet er in der eingestellten Zeit keinen Weg, wird das Level uebersprungen.

**Turbo:** Der Bot laesst die Level so schnell laufen, wie der PC es schafft (Standard bis 100x). In Szenen mit vielen Objekten wird er automatisch langsamer, statt dass das Spiel einfriert. Unter Windows bekommt GD dabei volle CPU-Leistung.

**Noclip muss in Eclipse (oder einem anderen Mod) an sein.** Den Speedhack braucht man mit Turbo nicht. In Classic-Leveln
gibt der Bot selbst keine Eingaben, er bedient nur das Menue.

- Startet nie auf dem geschuetzten Account (Einstellung, Standard: GTX980M)
- Blockiert standardmaessig das Hochladen der Stats an die Bestenlisten
- Merkt sich den Fortschritt und macht nach einem Absturz weiter
- Ueberspringt Level, die haengen bleiben, und schreibt sie in `uebersprungen.txt`
- ESC / Pause haelt den Bot an
