# Plan: Verhinderung von Message-Storms beim Tracing

## 1. Ausgangslage und Zielsetzung
Die aktuelle Tracing-Technologie kann unter hoher Last oder in Schleifen extrem viele Meldungen desselben Typs erzeugen (Message-Storms). Dies überlastet das I/O-System und erschwert die Analyse der Logs.
Ziel ist es, zusammengehörige bzw. gleichartige Log-Meldungen zu bündeln, wenn sie eine konfigurierbare Frequenz überschreiten. 

## 2. Anforderungen
- **Bündelung:** Treten zu viele Meldungen desselben "Typs" in kurzer Zeit auf, werden sie zusammengefasst.
- **Konfigurierbarkeit:** Die maximal zulässige Anzahl von Meldungen gleichen Typs pro Sekunde wird in der `broker.ini` als zentraler Wert konfiguriert.
- **Informationserhalt:** Ein ausgegebenes Bündel muss zwingend enthalten:
  - Den Inhalt der **ersten** Meldung des Bündels.
  - Den Inhalt der **letzten** Meldung des Bündels.
  - Die **Anzahl der "verschluckten" (unterdrückten) Meldungen** dazwischen.

## 3. Architektur & Design

### 3.1 Identifikation des Meldungstyps
Um Meldungen zuzuordnen, muss ein "Typ" definiert werden. Am effizientesten ist hierfür in C++ die Kombination aus Dateiname und Zeilennummer (`__FILE__` und `__LINE__`) der aufrufenden Tracing-Makros oder ein expliziter Tag/Kategorie-String. 

### 3.2 Konfiguration (`broker.ini`)
Erweiterung der Konfiguration um einen neuen Eintrag, z. B. unter einer `[tracing]` Sektion:
```ini
[tracing]
# Maximale Anzahl an Trace-Meldungen gleichen Typs, die pro Sekunde 
# ungebündelt ausgegeben werden. (0 = Rate-Limiting deaktiviert)
max_messages_per_type_per_sec = 10
```

### 3.3 Logik des Rate-Limiters
Es wird eine neue C++ Klasse/Komponente `TraceRateLimiter` eingeführt, die pro "Meldungstyp" folgenden Status hält:
- `window_start_time`: Zeitstempel, wann das aktuelle Ein-Sekunden-Fenster begann.
- `message_count`: Anzahl der Meldungen im aktuellen Fenster.
- `first_bundled_message`: Text der ersten Meldung, die das Limit überschritten hat.
- `last_bundled_message`: Text der aktuell letzten überschrittenen Meldung.

**Ablauf bei einer eintreffenden Meldung:**
1. Ermittle den Typ der Meldung.
2. Prüfe, ob die aktuelle Zeit > `window_start_time + 1 Sekunde` ist.
   - **Ja (neues Fenster):** 
     - Gab es im *alten* Fenster gebündelte Meldungen? Wenn ja, logge jetzt die Zusammenfassung: 
       `"[BUNDLED] <first_message> ... <count> weitere Meldungen unterdrückt ... <last_message>"`
     - Setze `window_start_time` auf jetzt, setze Zähler auf 1, logge die aktuelle Meldung normal.
   - **Nein (gleiches Fenster):**
     - Zähler iterieren (`message_count++`).
     - Ist `message_count <= max_messages_per_type_per_sec`? -> Normal ausgeben.
     - Ist `message_count > max_messages_per_type_per_sec`? -> Meldung nicht sofort loggen. 
       - Ist es die *erste* zu unterdrückende Meldung? -> Speichere als `first_bundled_message`.
       - Speichere immer als `last_bundled_message`.

## 4. Umsetzungs-Schritte im Code

1. **Konfigurationserweiterung:**
   - Parsen des neuen Werts (`max_messages_per_type_per_sec`) im Konfigurations-Modul.
   - Bereitstellung an das Tracing-System.
2. **TraceRateLimiter Implementierung:**
   - Implementierung der Datenstruktur zur Speicherung der Metadaten pro Typ (z. B. `std::unordered_map<MessageKey, BundleState>`).
   - Thread-Sicherheit durch Mutex oder Lock-free Mechanismen sicherstellen (da Tracing aus mehreren Threads erfolgen wird, vgl. `threading-refactoring`).
3. **Integration in den Trace-Sink/Makros:**
   - Einweben der `TraceRateLimiter::check(...)` Logik vor dem tatsächlichen Schreiben in die Datei/Konsole.
4. **Flushing Mechanismus:**
   - Da am Ende der Applikation oder in ruhigen Phasen noch nicht geloggte "letzte" Bündel im Speicher liegen könnten, muss ein Flush-Mechanismus (z.B. timer-basiert oder beim Shutdown) sicherstellen, dass ausstehende Zusammenfassungen geschrieben werden.
5. **Tests:**
   - Unit-Tests für den `TraceRateLimiter`: Prüfung des Ein-Sekunden-Fensters (Timings mocken), exaktes Limit prüfen, Korrektheit der Bündel-Outputs verifizieren.
6. **Dokumentation:**
   - Aktualisierung der `broker.ini` Templates und Readmes.
