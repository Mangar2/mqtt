# MQTT 5.0 Broker – Implementierungsplan

Vollständige Arbeitspakete für einen spezifikationskonformen MQTT 5.0 Broker.

---

## 1. Netzwerk & Transport

- 1.1 TCP-Server
  - 1.1.1 TCP-Listener (IPv4 / IPv6)
  - 1.1.2 Verbindungsannahme (Accept-Loop)
  - 1.1.3 Connection-Objekt (Lebenszyklus)
  - 1.1.4 Graceful Shutdown
- 1.2 TLS/SSL-Unterstützung
  - 1.2.1 TLS-Handshake
  - 1.2.2 Zertifikatsverwaltung
  - 1.2.3 Mutual TLS (Client-Zertifikate)
- 1.3 WebSocket-Transport
  - 1.3.1 WebSocket-Upgrade-Handshake
  - 1.3.2 MQTT-over-WebSocket Framing
- 1.4 I/O-Schicht
  - 1.4.1 Asynchrones Lesen (Stream-Buffer)
  - 1.4.2 Asynchrones Schreiben (Write-Queue)
  - 1.4.3 Backpressure-Mechanismus

---

## 2. Protokoll-Kodierung / Dekodierung

- 2.1 Basistypen
  - 2.1.1 Variable Byte Integer (Encode/Decode)
  - 2.1.2 Two Byte Integer
  - 2.1.3 Four Byte Integer
  - 2.1.4 UTF-8 String (Encode/Decode, Validierung)
  - 2.1.5 UTF-8 String Pair
  - 2.1.6 Binary Data
- 2.2 Fixed Header
  - 2.2.1 Pakettyp-Extraktion (Bits 7–4)
  - 2.2.2 Flags-Extraktion (Bits 3–0)
  - 2.2.3 Remaining Length (Encode/Decode)
- 2.3 Properties
  - 2.3.1 Property-Length (Variable Byte Integer)
  - 2.3.2 Property-Identifier Decode
  - 2.3.3 Property-Wert Decode (je Datentyp)
  - 2.3.4 Property-Wert Encode
  - 2.3.5 Validierung (Erlaubte Properties je Pakettyp)
  - 2.3.6 Duplikat-Erkennung (nicht wiederholbare Properties)
- 2.4 Paket-Decoder (alle 15 Typen)
  - 2.4.1 CONNECT
  - 2.4.2 CONNACK
  - 2.4.3 PUBLISH
  - 2.4.4 PUBACK
  - 2.4.5 PUBREC
  - 2.4.6 PUBREL
  - 2.4.7 PUBCOMP
  - 2.4.8 SUBSCRIBE
  - 2.4.9 SUBACK
  - 2.4.10 UNSUBSCRIBE
  - 2.4.11 UNSUBACK
  - 2.4.12 PINGREQ
  - 2.4.13 PINGRESP
  - 2.4.14 DISCONNECT
  - 2.4.15 AUTH
- 2.5 Paket-Encoder (alle 15 Typen)
  - 2.5.1 CONNECT
  - 2.5.2 CONNACK
  - 2.5.3 PUBLISH
  - 2.5.4 PUBACK
  - 2.5.5 PUBREC
  - 2.5.6 PUBREL
  - 2.5.7 PUBCOMP
  - 2.5.8 SUBSCRIBE
  - 2.5.9 SUBACK
  - 2.5.10 UNSUBSCRIBE
  - 2.5.11 UNSUBACK
  - 2.5.12 PINGREQ
  - 2.5.13 PINGRESP
  - 2.5.14 DISCONNECT
  - 2.5.15 AUTH
- 2.6 Paket-Validierung
  - 2.6.1 Malformed Packet Detection
  - 2.6.2 Reserved Bits Prüfung
  - 2.6.3 Pflichtfelder Prüfung
  - 2.6.4 Wertebereichs-Prüfung

---

## 3. Verbindungsmanagement

- 3.1 CONNECT-Verarbeitung
  - 3.1.1 Protokollname und -version prüfen
  - 3.1.2 Connect-Flags validieren
  - 3.1.3 Client-Identifier validieren / generieren
  - 3.1.4 Keep-Alive übernehmen
  - 3.1.5 Maximum Packet Size aushandeln
  - 3.1.6 Receive Maximum aushandeln
  - 3.1.7 Topic Alias Maximum aushandeln
  - 3.1.8 Doppel-CONNECT erkennen (Protocol Error)
- 3.2 CONNACK-Erzeugung
  - 3.2.1 Session Present Flag setzen
  - 3.2.2 Reason Code setzen
  - 3.2.3 Server-Capabilities als Properties befüllen
  - 3.2.4 Assigned Client Identifier Property
  - 3.2.5 Server Keep Alive Property
- 3.3 Session-Takeover
  - 3.3.1 Bestehende Verbindung mit gleicher Client-ID erkennen
  - 3.3.2 Alte Verbindung mit Reason Code 0x8E trennen
  - 3.3.3 Session-Zustand übernehmen oder verwerfen
- 3.4 DISCONNECT-Verarbeitung
  - 3.4.1 Client-initiiertes DISCONNECT (Normal)
  - 3.4.2 Client-initiiertes DISCONNECT mit Will (Reason Code 0x04)
  - 3.4.3 Server-initiiertes DISCONNECT
  - 3.4.4 Session Expiry Interval Override verarbeiten
- 3.5 Verbindungsstatus-Verwaltung
  - 3.5.1 Zustandsautomat (Connecting / Connected / Disconnecting)
  - 3.5.2 Abrupter Verbindungsabbruch erkennen

---

## 4. Session-Management

- 4.1 Session-Speicher
  - 4.1.1 In-Memory Session Store
  - 4.1.2 Persistenter Session Store (Datenbank / Dateisystem)
  - 4.1.3 Session-Lookup nach Client-ID
- 4.2 Session-Lebenszyklus
  - 4.2.1 Neue Session anlegen
  - 4.2.2 Bestehende Session wiederherstellen (Clean Start = 0)
  - 4.2.3 Session verwerfen (Clean Start = 1)
  - 4.2.4 Session Expiry Interval verwalten
  - 4.2.5 Abgelaufene Sessions bereinigen
- 4.3 Session-Zustand
  - 4.3.1 Subscription-Liste der Session
  - 4.3.2 Ausstehende QoS-1-Nachrichten (nicht bestätigt)
  - 4.3.3 Ausstehende QoS-2-Nachrichten (Zustandsmaschine)
  - 4.3.4 Packet-Identifier-Register der Session
  - 4.3.5 Offline-Nachrichten-Queue

---

## 5. Topic-Management

- 5.1 Topic-Validierung
  - 5.1.1 UTF-8 Zeichenvalidierung
  - 5.1.2 Wildcard-Verbot in PUBLISH Topics
  - 5.1.3 Null-Zeichen-Prüfung
  - 5.1.4 Maximallänge prüfen
- 5.2 Topic-Hierarchie
  - 5.2.1 Topic-Level-Parsing (Separator `/`)
  - 5.2.2 System-Topics (`$`-Prefix) isolieren
- 5.3 Wildcard-Matching
  - 5.3.1 Single-Level Wildcard (`+`) Matching
  - 5.3.2 Multi-Level Wildcard (`#`) Matching
  - 5.3.3 Kombinations-Matching (`sport/+/#`)
  - 5.3.4 System-Topic-Ausschluss aus Wildcards
- 5.4 Topic-Alias-Verwaltung
  - 5.4.1 Alias-Tabelle je Verbindung (eingehend)
  - 5.4.2 Alias-Tabelle je Verbindung (ausgehend)
  - 5.4.3 Alias anlegen (Topic + Alias-ID)
  - 5.4.4 Alias auflösen
  - 5.4.5 Alias-Limit (Topic Alias Maximum) durchsetzen
  - 5.4.6 Alias bei Verbindungsende zurücksetzen

---

## 6. Subscription-Management

- 6.1 Subscription-Speicher
  - 6.1.1 Subscription-Trie / Index-Datenstruktur
  - 6.1.2 Subscription hinzufügen
  - 6.1.3 Subscription entfernen
  - 6.1.4 Subscriber-Lookup für Topic
- 6.2 SUBSCRIBE-Verarbeitung
  - 6.2.1 Topic-Filter validieren
  - 6.2.2 Autorisierung prüfen
  - 6.2.3 Subscription Options verarbeiten
    - 6.2.3.1 Maximum QoS
    - 6.2.3.2 No Local Flag
    - 6.2.3.3 Retain As Published Flag
    - 6.2.3.4 Retain Handling (0 / 1 / 2)
  - 6.2.4 Subscription Identifier speichern
  - 6.2.5 SUBACK mit Reason Codes erzeugen
  - 6.2.6 Retained Messages bei neuem Subscribe senden
- 6.3 UNSUBSCRIBE-Verarbeitung
  - 6.3.1 Topic-Filter validieren
  - 6.3.2 Subscription entfernen
  - 6.3.3 UNSUBACK mit Reason Codes erzeugen
- 6.4 Shared Subscriptions
  - 6.4.1 Format `$share/gruppe/filter` parsen
  - 6.4.2 Subscription-Gruppe verwalten
  - 6.4.3 Nachrichtenverteilung innerhalb der Gruppe (Round-Robin / Load-Balancing)
  - 6.4.4 Gruppe auflösen wenn leer
- 6.5 Wildcard Subscriptions
  - 6.5.1 Wildcard-Flag in Subscription markieren
  - 6.5.2 Wildcard Subscription Available in CONNACK

---

## 7. Message Routing & Delivery

- 7.1 PUBLISH-Eingang (Client → Broker)
  - 7.1.1 Topic-Autorisierung prüfen
  - 7.1.2 Payload Format Indicator validieren
  - 7.1.3 Maximum Packet Size prüfen
  - 7.1.4 Topic Alias auflösen
  - 7.1.5 Subscriber-Liste ermitteln
  - 7.1.6 Nachricht an Subscriber verteilen
  - 7.1.7 No Local Filter anwenden
- 7.2 PUBLISH-Ausgang (Broker → Client)
  - 7.2.1 QoS-Downgrade auf abonniertes Maximum
  - 7.2.2 Subscription Identifier Property hinzufügen
  - 7.2.3 Retain As Published Flag anwenden
  - 7.2.4 Message Expiry Interval prüfen / anpassen
  - 7.2.5 Outbound-Queue je Client
- 7.3 Message Expiry
  - 7.3.1 Ablaufzeitpunkt berechnen (Eingang + Interval)
  - 7.3.2 Abgelaufene Nachrichten vor Zustellung verwerfen
  - 7.3.3 Verbleibende Zeit in ausgehendem Paket setzen
- 7.4 Offline-Nachrichtenspeicher
  - 7.4.1 Nachricht für offline Session puffern
  - 7.4.2 Nachrichten bei Reconnect zustellen
  - 7.4.3 Queue-Größenbeschränkung

---

## 8. QoS-Management

- 8.1 Packet-Identifier-Verwaltung
  - 8.1.1 Neuen Identifier vergeben (non-zero, unique)
  - 8.1.2 Identifier freigeben nach Abschluss
  - 8.1.3 Identifier-Konflikt erkennen (0x91)
  - 8.1.4 Getrennte ID-Räume (Client-seitig / Server-seitig)
- 8.2 QoS 0 – At Most Once
  - 8.2.1 Direktzustellung ohne Speicherung
- 8.3 QoS 1 – At Least Once
  - 8.3.1 PUBLISH speichern bis PUBACK
  - 8.3.2 PUBACK empfangen und verarbeiten
  - 8.3.3 PUBACK senden (Broker → Publisher)
  - 8.3.4 Retransmission bei ausbleibender Bestätigung
  - 8.3.5 DUP-Flag bei Retransmission setzen
- 8.4 QoS 2 – Exactly Once
  - 8.4.1 PUBLISH empfangen, PUBREC senden
  - 8.4.2 PUBREL empfangen, PUBCOMP senden
  - 8.4.3 PUBREC empfangen, PUBREL senden
  - 8.4.4 PUBCOMP empfangen, Zustand bereinigen
  - 8.4.5 Duplikat-Erkennung (bereits empfangene Packet-IDs)
  - 8.4.6 Retransmission-Zustandsmaschine
  - 8.4.7 PUBREC / PUBREL mit Fehler-Reason-Code verarbeiten
- 8.5 Inflight-Limit (Receive Maximum)
  - 8.5.1 Inflight-Counter je Verbindung
  - 8.5.2 Sende-Stopp bei Erreichen des Limits
  - 8.5.3 Sende-Freigabe nach Bestätigung
  - 8.5.4 Protokollfehler bei Überschreitung (0x93)

---

## 9. Retained Messages

- 9.1 Retained Message Store
  - 9.1.1 In-Memory Store (Topic → Nachricht)
  - 9.1.2 Persistenter Store
  - 9.1.3 Retained Message speichern / überschreiben
  - 9.1.4 Retained Message löschen (leeres Payload)
- 9.2 Retained Message Zustellung
  - 9.2.1 Passende Retained Messages bei Subscribe suchen
  - 9.2.2 Retain Handling Option auswerten
  - 9.2.3 RETAIN-Flag im ausgehenden PUBLISH setzen (Retain As Published)
  - 9.2.4 Message Expiry bei Retained Messages prüfen
- 9.3 Retain Available Feature
  - 9.3.1 Retain Available in CONNACK setzen
  - 9.3.2 RETAIN=1 ablehnen wenn nicht unterstützt (0x9A)

---

## 10. Will Messages

- 10.1 Will-Konfiguration
  - 10.1.1 Will-Daten aus CONNECT-Payload lesen
  - 10.1.2 Will Properties verarbeiten (Delay, Expiry, Format, etc.)
  - 10.1.3 Will-Daten in Session speichern
- 10.2 Will-Auslösung
  - 10.2.1 Abrupter Verbindungsabbruch erkennen
  - 10.2.2 Keep-Alive-Timeout als Auslöser
  - 10.2.3 Will Delay Interval abwarten
  - 10.2.4 Will Message publizieren
  - 10.2.5 Will unterdrücken bei normalem DISCONNECT (Reason 0x00)
  - 10.2.6 Will publizieren bei DISCONNECT mit Reason 0x04
- 10.3 Will Delay bei Session-Persistenz
  - 10.3.1 Will Delay Timer starten nach Disconnect
  - 10.3.2 Timer abbrechen bei Reconnect vor Ablauf
  - 10.3.3 Will sofort publizieren wenn Session abläuft vor Will Delay

---

## 11. Authentifizierung

- 11.1 Username / Password Authentifizierung
  - 11.1.1 Credentials aus CONNECT lesen
  - 11.1.2 Validierungsschnittstelle (Plugin / Callback)
  - 11.1.3 CONNACK mit Reason 0x86 bei Fehlschlag
- 11.2 Enhanced Authentication (AUTH-Paket)
  - 11.2.1 Authentication Method aus CONNECT lesen
  - 11.2.2 AUTH-Paket senden (Continue, Reason 0x18)
  - 11.2.3 AUTH-Paket empfangen und verarbeiten
  - 11.2.4 Mehrstufiger Handshake (Challenge-Response)
  - 11.2.5 CONNACK bei Erfolg
  - 11.2.6 CONNACK mit 0x8C bei unbekannter Method
  - 11.2.7 Re-Authentifizierung (Reason 0x19) während aktiver Session
- 11.3 Anonymer Zugang
  - 11.3.1 Konfigurierbare Erlaubnis für anonyme Verbindungen

---

## 12. Autorisierung

- 12.1 Publish-Autorisierung
  - 12.1.1 Topic-basierte ACL prüfen
  - 12.1.2 PUBACK / PUBREC mit Reason 0x87 bei Ablehnung
- 12.2 Subscribe-Autorisierung
  - 12.2.1 Topic-Filter-basierte ACL prüfen
  - 12.2.2 SUBACK mit Reason 0x87 je abgelehntem Filter
- 12.3 ACL-Verwaltung
  - 12.3.1 ACL-Datenstruktur (User / Topic / Berechtigung)
  - 12.3.2 ACL-Lade-Mechanismus (Datei / Datenbank)
  - 12.3.3 Wildcard-Unterstützung in ACL-Regeln

---

## 13. Keep Alive & Heartbeat

- 13.1 Keep-Alive-Timer
  - 13.1.1 Keep-Alive-Wert aus CONNECT übernehmen
  - 13.1.2 Server Keep Alive Override (CONNACK Property 0x13)
  - 13.1.3 Inaktivitäts-Deadline berechnen (1,5 × Keep Alive)
  - 13.1.4 Timer zurücksetzen bei jedem eingehenden Paket
  - 13.1.5 Verbindung trennen bei Timeout (Reason 0x8D)
- 13.2 PINGREQ / PINGRESP
  - 13.2.1 PINGREQ empfangen und mit PINGRESP antworten
  - 13.2.2 PINGRESP-Timing prüfen

---

## 14. Flow Control

- 14.1 Receive Maximum
  - 14.1.1 Receive Maximum in CONNECT lesen
  - 14.1.2 Receive Maximum in CONNACK setzen
  - 14.1.3 Inflight-Fenster eingehend durchsetzen
  - 14.1.4 Inflight-Fenster ausgehend durchsetzen
- 14.2 Maximum Packet Size
  - 14.2.1 Maximum Packet Size aus CONNECT lesen
  - 14.2.2 Maximum Packet Size in CONNACK setzen
  - 14.2.3 Paket vor Versand auf Größe prüfen
  - 14.2.4 Zu großes eingehendes Paket ablehnen (0x95)
- 14.3 Quota & Rate Limiting
  - 14.3.1 Verbindungsrate begrenzen (0x9F)
  - 14.3.2 Nachrichtenrate begrenzen (0x96)
  - 14.3.3 Quota überwachen (0x97)

---

## 15. Error Handling & Reason Codes

- 15.1 Malformed Packet Handling
  - 15.1.1 Dekodierungsfehler abfangen
  - 15.1.2 DISCONNECT mit 0x81 senden
  - 15.1.3 Verbindung schließen
- 15.2 Protocol Error Handling
  - 15.2.1 Unzulässige Zustandsübergänge erkennen
  - 15.2.2 DISCONNECT mit 0x82 senden
- 15.3 Reason Code Vollständigkeit
  - 15.3.1 Alle 39 Reason Codes implementieren
  - 15.3.2 Reason String Property (0x1F) befüllen
  - 15.3.3 Request Problem Information auswerten
- 15.4 Server-Fehlerbehandlung
  - 15.4.1 Interne Fehler (0x83) an Client melden
  - 15.4.2 Server Unavailable (0x88) bei Überlast
  - 15.4.3 Server Shutting Down (0x8B) bei gesteuertem Stopp

---

## 16. Server Capabilities & Negotiation

- 16.1 CONNACK Capabilities
  - 16.1.1 Maximum QoS (0x24)
  - 16.1.2 Retain Available (0x25)
  - 16.1.3 Wildcard Subscription Available (0x28)
  - 16.1.4 Subscription Identifier Available (0x29)
  - 16.1.5 Shared Subscription Available (0x2A)
  - 16.1.6 Maximum Packet Size (0x27)
  - 16.1.7 Receive Maximum (0x21)
  - 16.1.8 Topic Alias Maximum (0x22)
  - 16.1.9 Session Expiry Interval (0x11)
  - 16.1.10 Response Information (0x1A)
- 16.2 Server Redirection
  - 16.2.1 Server Reference Property (0x1C)
  - 16.2.2 Use Another Server (0x9C)
  - 16.2.3 Server Moved (0x9D)

---

## 17. Request / Response Pattern

- 17.1 Response Topic (Property 0x08)
  - 17.1.1 Response Topic aus PUBLISH lesen
  - 17.1.2 Response Topic an Subscriber weiterleiten
- 17.2 Correlation Data (Property 0x09)
  - 17.2.1 Correlation Data transparent weiterleiten
- 17.3 Response Information (Property 0x1A in CONNACK)
  - 17.3.1 Response Information erzeugen wenn angefordert (0x19)

---

## 18. Persistenz

- 18.1 Session-Persistenz
  - 18.1.1 Session-Zustand serialisieren
  - 18.1.2 Session-Zustand deserialisieren / laden
  - 18.1.3 Crash-Recovery
- 18.2 Retained Message Persistenz
  - 18.2.1 Retained Messages speichern
  - 18.2.2 Retained Messages beim Start laden
- 18.3 Inflight-Message Persistenz
  - 18.3.1 QoS-1/2-Zustand speichern
  - 18.3.2 Nach Neustart fortsetzen

---

## 19. Monitoring & Diagnostics

- 19.1 System-Topics (`$SYS`)
  - 19.1.1 `$SYS/broker/version`
  - 19.1.2 `$SYS/broker/uptime`
  - 19.1.3 `$SYS/broker/clients/connected`
  - 19.1.4 `$SYS/broker/clients/total`
  - 19.1.5 `$SYS/broker/messages/received`
  - 19.1.6 `$SYS/broker/messages/sent`
  - 19.1.7 `$SYS/broker/subscriptions/count`
  - 19.1.8 `$SYS/broker/retained messages/count`
  - 19.1.9 Veröffentlichungsintervall konfigurieren
- 19.2 Logging
  - 19.2.1 Verbindungsereignisse loggen
  - 19.2.2 Fehler und Protokollverstöße loggen
  - 19.2.3 Log-Level konfigurieren

---

## 20. Konfiguration & Administration

- 20.1 Konfigurationsdatei
  - 20.1.1 Port(s) konfigurieren (MQTT, MQTTS, WS, WSS)
  - 20.1.2 TLS-Zertifikatspfade
  - 20.1.3 Max. Verbindungen
  - 20.1.4 Session Expiry Default
  - 20.1.5 Max. Inflight-Nachrichten
  - 20.1.6 Max. Retained Messages
  - 20.1.7 Persistenz aktivieren / deaktivieren
  - 20.1.8 Authentifizierungsbackend konfigurieren
  - 20.1.9 ACL-Quelle konfigurieren
- 20.2 Laufzeit-Administration
  - 20.2.1 Clients auflisten
  - 20.2.2 Client trennen
  - 20.2.3 Subscriptions einsehen
  - 20.2.4 Retained Messages löschen
  - 20.2.5 Broker geordnet herunterfahren

---

## 21. Tests & Konformität

- 21.1 Unit-Tests
  - 21.1.1 Encoder / Decoder Tests (alle Pakettypen)
  - 21.1.2 Wildcard-Matching Tests
  - 21.1.3 QoS-Zustandsmaschinen Tests
  - 21.1.4 Session-Expiry Tests
- 21.2 Integrationstests
  - 21.2.1 Connect / Disconnect Flows
  - 21.2.2 QoS 0 / 1 / 2 End-to-End
  - 21.2.3 Retained Message Flows
  - 21.2.4 Will Message Flows
  - 21.2.5 Shared Subscription Flows
  - 21.2.6 AUTH Handshake Flows
  - 21.2.7 Session-Persistenz nach Reconnect
- 21.3 Konformitätstest-Suite
  - 21.3.1 MQTT 5.0 OASIS Test-Cases
  - 21.3.2 Interoperabilitätstests mit bekannten Clients (Paho, Mosquitto)
