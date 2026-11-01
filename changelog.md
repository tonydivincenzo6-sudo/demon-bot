# v1.5.0
- Neu: Turbo. Der Bot rechnet pro Bild so viele Spielschritte, wie in die Rechenzeit passen. Leere Szenen laufen
  sehr schnell (bis zum eingestellten Maximaltempo, Standard 100x), Szenen mit vielen Objekten automatisch langsamer,
  statt dass das Spiel einfriert. Der Speedhack in Eclipse wird dafuer nicht gebraucht.
- Neu (Windows): GD wird nicht mehr gedrosselt, solange der Bot laeuft (kein Effizienzmodus, hoehere Prioritaet),
  auch wenn das Fenster im Hintergrund liegt
- Platformer: der saubere Lauf am Ende nutzt ebenfalls den Turbo

# v1.4.0
- Platformer-Bot deutlich staerker:
  - zaehlt Level-Checkpoints und eingesammelte Items (Schluessel) als Fortschritt, nicht nur wie weit rechts er ist
  - sucht den End-Trigger im Level und laeuft in dessen Richtung
  - merkt sich Stellen, an denen er am Boden steht (von dort kann er springen)
  - etwa 3-4x schneller: 4 Physik-Ticks pro Schritt, das Level wird waehrend der Suche nicht gezeichnet
  - sucht laenger, solange er noch weiterkommt (bis zur doppelten Suchzeit)
  - klappt das Nachspielen nicht, sucht er ab dieser Stelle weiter, statt das Level zu ueberspringen
- Standard: 8 Minuten Suchzeit, 600 Spielstaende
- In `uebersprungen.txt` steht jetzt, wie weit der Bot gekommen ist (z. B. 3/5 Checkpoints)

# v1.3.0
- Neu: Platformer-Level (Auswahl oben im Fenster: Classic und/oder Platformer)
- Platformer: Der Bot sucht selbst einen Weg zum Ziel und spielt ihn dann sauber von vorne
- Einstellungen fuer Platformer-Suchzeit und Speicher

# v1.2.0
- Auswahl-Fenster: jede Schwierigkeit einzeln an/aus (Auto bis Extreme Demon)
- Schnellknoepfe: Alle, Nur Demons, Keine
- Auswahl wird gemerkt

# v1.1.0
- Neu: Auto, Easy, Normal, Hard, Harder und Insane
- Beim Start auswaehlen: nur Demons oder alle bewerteten Level
- Alter Fortschritt aus v1.0.0 wird uebernommen

# v1.0.0
- Erste Version: alle bewerteten Demons
