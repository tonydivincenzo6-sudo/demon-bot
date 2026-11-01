# Turbo Power

Laesst Level so schnell laufen, wie der PC es gerade schafft, standardmaessig bis **100x**.

**Warum nicht einfach Speedhack?** Ein Speedhack verlangt immer dasselbe Tempo. Kommt eine Szene mit vielen Objekten,
dauert ein Bild laenger, das naechste muss noch mehr nachrechnen, und das Spiel schaukelt sich auf, bis es einfriert.
Turbo Power rechnet stattdessen pro Bild nur so viele Spielschritte, wie in eine feste Rechenzeit passen (Standard 30 ms).
Leere Stellen laufen mit vollem Tempo, volle Szenen automatisch langsamer, und das Spiel bleibt bedienbar.
Unten rechts steht, wie schnell es gerade wirklich laeuft.

**Windows:** Solange der Turbo laeuft, wird GD nicht gedrosselt (kein "Effizienzmodus", auch nicht im Hintergrund
beim Aufnehmen), bekommt eine hoehere Prioritaet, und Windows schaltet auf den Energiesparplan **Hoechstleistung**
(oder den Energiemodus "Beste Leistung"), damit die CPU hochtaktet. 20 Sekunden nach dem Turbo und beim Beenden kommt
dein alter Plan zurueck. Stuerzt GD ab, stellt die Mod ihn beim naechsten Start zurueck.

**Mit dem Demon Bot:** Standardmaessig ist der Turbo nur an, wenn der Demon Bot das Level spielt. Bei Platformer-Leveln
laeuft die Wegsuche des Bots mit voller Leistung, der saubere Lauf danach im Turbo. Den Speedhack von Eclipse kannst du
ausschalten, Noclip muss fuer Classic-Level weiter an sein.

- **F8** schaltet den Turbo an/aus, ohne zu pausieren (Taste in den Einstellungen aenderbar)
- Ton ist im Turbo aus und kommt danach von selbst zurueck
- Hoechsttempo und Rechenzeit pro Bild sind einstellbar
