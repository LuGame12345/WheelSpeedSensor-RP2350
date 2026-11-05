# BLE-Schnittstelle Auto-Roboter – Anleitung für die Handy-App

Dieses Dokument beschreibt alles, was eine Handy-App braucht, um den Roboter per Bluetooth zu steuern (Joystick) und seine Messwerte anzuzeigen. Es ist die verbindliche Schnittstelle zwischen Firmware und App.

- **Firmware-Stand:** Branch `app`, Protokollversion `2`
- **App-Konzept:** `docs/App_PS4_Controller_Konzept.pdf` (iOS-App mit PS4-Controller: Tastenbelegung, Modi, Not-Halt)
- **Gerät:** Raspberry Pi Pico 2 W (RP2350 + Funkchip CYW43439), Arduino-Firmware mit BTstack

---

## 1. Bluetooth-Technik – das Wichtigste vorweg

| Punkt | Wert |
|---|---|
| Technologie | **Bluetooth Low Energy (BLE)**, GATT-Peripheral. **Kein** Bluetooth Classic, kein SPP/RFCOMM |
| Gerätename | `Auto-Roboter` |
| Pairing / Bonding | **Keins.** Keine PIN, keine Kopplung, keine Verschlüsselung |
| Gleichzeitige Verbindungen | **1** (solange ein Handy verbunden ist, ist der Roboter für andere unsichtbar) |
| Datenformat | UTF-8-Text, zeilenbasiert (`\n`) |
| Service | Nordic UART Service (NUS), ein De-facto-Standard für „Seriell über BLE“ |

### Warum der Roboter nicht in den Bluetooth-Einstellungen auftaucht

- **iPhone/iOS:** Die Bluetooth-Einstellungen zeigen nur Geräte, die iOS selbst kennt (Kopfhörer, Tastaturen …). Ein reines BLE-Gerät wie dieser Roboter erscheint dort **nie**. Die Verbindung wird **ausschließlich in der App** über **CoreBluetooth** aufgebaut. Das ist normal und kein Fehler.
- **Android:** Auch hier taucht er in den Systemeinstellungen oft nicht oder nur ohne Funktion auf. Die Verbindung läuft in der App über die BLE-API (`BluetoothLeScanner`, `BluetoothGatt`).
- **Nicht** über die Systemeinstellungen koppeln. Ist er dort doch gekoppelt worden: wieder entfernen.

### Was die App dafür braucht

**iOS**
- `Info.plist`: `NSBluetoothAlwaysUsageDescription` (Begründungstext, sonst stürzt die App beim ersten BLE-Zugriff ab)
- Framework: CoreBluetooth (`CBCentralManager`, `CBPeripheral`)
- Hinweis: iOS zeigt keine MAC-Adresse. Geräte werden über `peripheral.identifier` (UUID, pro Handy verschieden), den Namen oder die Service-UUID erkannt.

**Android**
- Android 12+ (API 31+): Laufzeitrechte `BLUETOOTH_SCAN` (mit `neverForLocation`) und `BLUETOOTH_CONNECT`
- Android 11 und älter: `BLUETOOTH`, `BLUETOOTH_ADMIN`, `ACCESS_FINE_LOCATION` (Standort muss eingeschaltet sein, sonst findet der Scan nichts)
- Nach dem Verbinden **MTU anfordern** (`requestMtu(185)`), damit Nachrichten in weniger Stücken kommen (siehe Abschnitt 3)

**Cross-Platform-Optionen** (alle mit NUS gängig):
- Flutter: `flutter_blue_plus`
- React Native: `react-native-ble-plx` (mit Expo nur als *Development Build*, **nicht** in Expo Go)
- Native: Swift + CoreBluetooth / Kotlin + Android BLE
- **Nicht geeignet:** Web Bluetooth im Browser auf dem iPhone (Safari unterstützt es nicht)

---

## 2. GATT-Profil

### Advertising (beim Scannen sichtbar)

| Feld | Inhalt |
|---|---|
| Advertising-Daten | Flags + vollständiger Name `Auto-Roboter` |
| Scan-Response | 128-bit-Service-UUID des NUS: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |

**Empfehlung:** Scan mit Filter auf die Service-UUID `6E400001-…`. Falls eine Plattform den Filter nicht auf die Scan-Response anwendet: ungefiltert scannen und nach dem Namen `Auto-Roboter` filtern.

### Services und Characteristics

| Service / Characteristic | UUID | Eigenschaften | Richtung |
|---|---|---|---|
| **Nordic UART Service** | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` | – | – |
| RX (Befehle an den Roboter) | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` | Write, **Write Without Response** | App → Roboter |
| TX (Daten vom Roboter) | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` | **Notify** | Roboter → App |
| Generic Access → Device Name | `0x2A00` | Read | – |

**Wichtig:** Der Roboter sendet **nur**, wenn die App auf TX die **Notifications aktiviert** hat (CCCD = `0x0001`; in den Bibliotheken meist `setNotifyValue(true)` / `setCharacteristicNotification` bzw. `monitorCharacteristic…`). Ohne das kommt nichts an.

---

## 3. Übertragung: Zeilen und Stücke

### Roboter → App (TX, Notify)

- Jede Nachricht ist **eine Zeile**, abgeschlossen mit `\n`.
- Eine Zeile kann **auf mehrere Notifications verteilt** ankommen (max. *MTU − 3* Bytes pro Notification; ohne MTU-Erhöhung nur **20 Bytes**).
- Die App muss deshalb **puffern**: empfangene Bytes anhängen, bei jedem `\n` eine vollständige Zeile herauslösen und verarbeiten.
- Zeichensatz UTF-8 (Meldungen enthalten Umlaute und `→`).
- Ohne Verbindung oder bei vollem Puffer verwirft der Roboter Daten (er blockiert nie).

### App → Roboter (RX, Write)

- Befehle sind ASCII-Text.
- **Write Without Response** verwenden (schneller, für den Joystick wichtig). Normales Write geht auch.
- Erweiterte Befehle beginnen mit `$` und enden mit `\n` (alternativ `;`), z. B. `$J,40,80\n`.
- Maximal 47 Zeichen nach dem `$`.
- Ein Befehl pro Write ist am einfachsten; mehrere hintereinander in einem Write sind erlaubt.

---

## 4. Eingänge – Befehle an den Roboter

### 4.1 App-Befehle (empfohlen für die App)

| Befehl | Argumente | Wirkung | Antwort (JSON-Modus) |
|---|---|---|---|
| `$J,<x>,<y>` | x, y: ganze Zahlen −100 … 100 | **Joystick** (siehe Abschnitt 6) | – (Status zeigt `"mode":"joystick"`) |
| `$J,0,0` | – | Joystick losgelassen → sofort Stopp | – |
| `$STOP` | – | Anhalten (ausrollen) | – |
| `$BRAKE` | – | Aktiv bremsen | – |
| `$SPEED,<n>` | n: 10 … 100 | Höchsttempo des Joysticks in % (Standard 70) | `{"t":"speed","max":70}` |
| `$FMT,JSON` | – | Ausgaben an BLE als **JSON-Zeilen** | `{"t":"fmt","value":"JSON"}` |
| `$FMT,TEXT` | – | Ausgaben an BLE als lesbarer Text (Standard) | Textzeile |
| `$PING` | – | Verbindungstest | `{"t":"pong"}` |
| `$INFO` | – | Geräteinfo | `{"t":"info","name":"Auto-Roboter","proto":2,"maxSpeed":70,"joyTimeoutMs":300,"hbTimeoutMs":1500,"selected":"manual","estop":false,"lineSpeed":40,"lineKp":0.60}` |
| `$MODE,MANUAL` | – | Modus Manuell (L1/R1). Hält an (ausrollen) | `{"t":"mode","value":"manual"}` |
| `$MODE,LINE` | – | Modus Autonom/Linienfolge (L1/R1). Hält an, wartet auf `$START` | `{"t":"mode","value":"line"}` |
| `$START` | – | **X:** Ist der Not-Halt aktiv → nur Sperre lösen (fährt nicht los). Sonst im Modus `line` → Linienfolge starten | `{"t":"estop","active":false}` bzw. `{"t":"start","mode":"line"}` / `{"t":"start","mode":"manual"}` |
| `$ESTOP` | – | **Dreieck – Not-Halt:** aktiv bremsen (Motorklemmen kurzgeschlossen), Linienfolge beenden, **alle Fahrbefehle gesperrt** bis `$START` | `{"t":"estop","active":true}` |
| `$LINESPEED,<n>` | n: 10 … 100 | Tempo der Linienfolge in % (Steuerkreuz ↑/↓) | `{"t":"linespeed","value":40}` |
| `$LINEKP,<v>` | v: 0 … 5 (Kommazahl, z. B. `0.7`) | Lenkstärke der Linienfolge (Steuerkreuz ←/→) | `{"t":"linekp","value":0.70}` |
| `$BIAS,<v>` | v: −20 … 20 (%) | Kurs-Trim für Geradeauslauf im Modus Manuell | `{"t":"bias","value":0.5}` |
| `$TRIMRESET` | – | Gelernten Motor-Trim löschen (Touchpad-Klick), nur im Stand | `msg` oder `error` „nur im Stand“ |
| `$HB` | – | **Heartbeat**, alle 500 ms senden (siehe 4.3) | – |

Unbekannte Befehle → `{"t":"error","msg":"unbekannter Befehl","cmd":"…"}`.

**Gesperrt während Not-Halt:** `$J`, Fahrtasten `w/a/s/d`, `l` und `$START` als Startbefehl (das erste `$START` löst nur die Sperre). `$STOP` und `$BRAKE` ändern nichts an der Sperre.

**`$J` nur im Modus Manuell:** Im Modus `line` wird der Joystick ignoriert (R2/Stick fahren nur manuell).

### 4.2 Heartbeat

- Die App sendet `$HB\n` alle **500 ms**, solange sie verbunden ist – in jedem Modus.
- Sobald der Pico einmal ein `$HB` bekommen hat, gilt: Kommt **1,5 s** lang **kein `$`-Befehl** (jeder `$`-Befehl zählt), hält der Pico alles an – **auch die Linienfolge** – und meldet `{"t":"hbtimeout"}`.
- Ohne jemals `$HB` (z. B. Test mit einem Terminal) ist der Heartbeat nicht aktiv.
- Nach einem Verbindungsabbruch muss die App den Heartbeat wieder beginnen.

### 4.3 Controller-Belegung → Befehle (aus dem App-Konzept)

| PS4-Taste | Befehl |
|---|---|
| R2 / L2 (analog) + linker Stick | `$J,<x>,<y>` mit x = Stick · 100, y = (R2 − L2) · 100 (nur Modus Manuell) |
| L1 / R1 | `$MODE,MANUAL` / `$MODE,LINE` |
| X | `$START` |
| Kreis | `$STOP` (Aus: ausrollen, beendet auch die Linienfolge) |
| Dreieck | `$ESTOP` (Not-Halt) – sofort, ohne Warteschlange |
| Quadrat | Feinmodus: `$SPEED,30` an / vorheriges Tempo wieder aus |
| Steuerkreuz ↑ / ↓ | `$LINESPEED,<n±10>` (Tempo im Autonom-Modus) |
| Steuerkreuz ← / → | `$LINEKP,<v±0.1>` (Lenkstärke im Autonom-Modus) |
| Touchpad klicken | `$TRIMRESET` |
| Touchpad wischen, Options | nur App (Ansicht wechseln / Einstellungen), kein Befehl |

Aktuelle Werte für ↑/↓ und ←/→ stehen in jeder `status`-Zeile (`lineSpeed`, `lineKp`).

### 4.4 Einzelzeichen-Befehle (Terminal, Tastatur, zum Testen)

Diese funktionieren weiterhin, z. B. für Tests mit einer Terminal-App. Für die eigene App sind die `$`-Befehle gedacht.

| Zeichen | Wirkung |
|---|---|
| `w` `s` `a` `d` | Vor / zurück / links drehen / rechts drehen – **nur solange wiederholt gesendet** (erste Wiederholung innerhalb 600 ms, danach Abstand ≤ 200 ms) |
| `x` | Stopp |
| `b` | Bremsen |
| `+` / `-` | Tempo der Tasten ±10 % |
| `,` / `.` | Kurs-Trim links / rechts (Geradeauslauf) |
| `p` / `P`, `i` / `I` | Regler-Parameter Kp / Ki −/+ |
| `r` | Gelernten Trim zurücksetzen |
| `n` | Gleichlaufregelung an/aus |
| `g` | Einstellungen anzeigen |
| `h` | Hilfe |

Außerdem versteht der Roboter das **Control Pad von „Bluefruit Connect“** (`!B…`-Pakete).

---

## 5. Ausgänge – Daten vom Roboter

Direkt nach dem Verbinden sendet der Roboter **lesbaren Text**. Die App schaltet mit `$FMT,JSON\n` auf **JSON-Zeilen** um. Nach einem Verbindungsabbruch steht das Format wieder auf Text, die App muss es also **nach jedem Verbinden** neu setzen.

Jede JSON-Zeile hat das Feld `"t"` (Typ).

### 5.1 `status` – Messwerte, 5× pro Sekunde (alle 200 ms)

```json
{"t":"status","ms":123456,"mode":"joystick","line":[0,1,0],"dist":0.153,"pwm":[45.0,45.0],"speed":[0.210,0.205],"pulses":[412,409],"err":1.0,"trim":2.5,"maxSpeed":70,"selected":"manual","estop":false,"gap":false,"blocked":false,"lineSpeed":40,"lineKp":0.60}
```

| Feld | Typ | Einheit | Bedeutung |
|---|---|---|---|
| `ms` | int | ms | Zeit seit Start des Roboters |
| `mode` | string | – | Was gerade fährt: `idle` (steht), `joystick` (App-Joystick), `keys` (Tastensteuerung), `line` (Linienfolge läuft) |
| `selected` | string | – | Gewählter Modus: `manual` oder `line` (per `$MODE`) |
| `estop` | bool | – | `true` = Not-Halt aktiv, alles gesperrt bis `$START` |
| `gap` | bool | – | `true` = Linienfolge sieht gerade keine Linie und fährt geradeaus (Lücke) |
| `blocked` | bool | – | `true` = steht wegen eines Hindernisses (Linienfolge pausiert) |
| `lineSpeed` | int | % | Tempo der Linienfolge |
| `lineKp` | float | – | Lenkstärke der Linienfolge |
| `line` | [int, int, int] | 0/1 | Spurfolgesensor links, Mitte, rechts; `1` = schwarze Linie erkannt |
| `dist` | float \| `null` | m | Abstand zum Hindernis vorne (Ultraschall); `null` = kein Echo (frei > 4 m oder zu nah < 3 cm) |
| `pwm` | [float, float] | % | Aktueller Sollwert links / rechts, −100 … 100 (negativ = rückwärts), nach Hindernis-Drosselung |
| `speed` | [float, float] | m/s | Gemessene Radgeschwindigkeit links / rechts (**immer ≥ 0**, die Sensoren kennen keine Richtung) |
| `pulses` | [int, int] | Pulse | Lichtschranken-Pulse seit dem letzten neuen Fahrbefehl (1 Puls ≈ 10 mm) |
| `err` | float | Pulse | Gleichlauf-Fehler (> 0: links voraus) |
| `trim` | float | % | Gelernte Motor-Korrektur |
| `maxSpeed` | int | % | Joystick-Höchsttempo |

### 5.2 Ereignisse

| Typ | Beispiel | Wann |
|---|---|---|
| `obstacle` | `{"t":"obstacle","dist":0.071,"brakeAt":0.074,"speed":0.16}` | Roboter hat wegen eines Hindernisses gebremst (Abstände in m, Tempo in m/s) |
| `timeout` | `{"t":"timeout"}` | Joystick-Befehle blieben > 300 ms aus → Roboter hat angehalten |
| `hbtimeout` | `{"t":"hbtimeout"}` | Heartbeat ausgeblieben (> 1,5 s kein `$`-Befehl) → alles angehalten, auch Linienfolge |
| `estop` | `{"t":"estop","active":true}` | Not-Halt an (`true`) bzw. gelöst (`false`) |
| `start` | `{"t":"start","mode":"line"}` | Start bestätigt (`line` = Linienfolge läuft, `manual` = bereit) |
| `mode` | `{"t":"mode","value":"line"}` | Modus gewechselt |
| `linespeed`, `linekp`, `bias` | `{"t":"linespeed","value":50}` | Neue Einstellung bestätigt |
| `msg` | `{"t":"msg","text":"Trim zurückgesetzt"}` | Freitext (Hilfe, Bestätigungen) – zum Anzeigen oder Ignorieren |
| `speed`, `fmt`, `pong`, `info`, `error` | siehe Tabelle 4.1 | Antworten auf Befehle |

Bei einem Hindernis-Stopp kommt `obstacle` **einmal**; solange er steht, ist `blocked` in der status-Zeile `true`. Im Modus Linie fährt er von selbst weiter, sobald der Weg über 10 cm frei ist.

Die App soll **unbekannte Typen und Felder ignorieren** (spätere Firmware kann mehr senden).

### 5.3 Textformat (Standard, für Menschen)

Ohne `$FMT,JSON` kommt z. B.:

```
Linie: .#. | Hindernis: 0.153 m | PWM L/R: 45.0% / 45.0% | Pulse L/R: 412/409 | Fehler: 1.0 Pulse | Trim: 2.5% || L: 0.210 m/s | R: 0.205 m/s
```

Nicht zum Parsen in der App gedacht – dafür gibt es JSON.

---

## 6. Joystick-Steuerung

### Koordinaten

```
            y = +100 (vor)
                 ▲
 x = −100 ◄──────┼──────► x = +100
  (links)        │        (rechts)
                 ▼
            y = −100 (zurück)
```

- Werte als **ganze Zahlen** von −100 bis 100 senden.
- Mittelstellung = `0,0`.
- **Totzone:** Beträge unter 8 werden vom Roboter als 0 behandelt.

### Senderegeln (wichtig für die Sicherheit)

1. **Solange der Finger auf dem Joystick ist:** `$J,<x>,<y>\n` **alle 50–100 ms** senden (10–20 Hz), **auch wenn sich der Wert nicht ändert**. Das ist ein „Totmannschalter“.
2. **Beim Loslassen:** sofort einmal `$J,0,0\n` senden.
3. **Kommt 300 ms lang kein `$J`**, hält der Roboter selbst an und meldet `{"t":"timeout"}` (z. B. wenn die App einfriert oder in den Hintergrund geht).
4. **Nicht schneller als alle 50 ms** senden, sonst staut sich die BLE-Verbindung.
5. Write Without Response verwenden.

### So rechnet der Roboter (zur Info)

```
links  = y + x
rechts = y − x
→ falls |links| oder |rechts| > 100: beide auf ±100 herunterskalieren
→ × Höchsttempo ($SPEED, Standard 70 %)
→ auf 2-%-Schritte gerundet
```

Beispiele bei Höchsttempo 70 %:

| Joystick (x, y) | links | rechts | Bewegung |
|---|---|---|---|
| (0, 100) | 70 % | 70 % | geradeaus vor |
| (0, −100) | −70 % | −70 % | geradeaus zurück |
| (100, 0) | 70 % | −70 % | auf der Stelle rechts drehen |
| (50, 100) | 70 % | 24 % | Rechtskurve vorwärts |

### Tempo-Regler in der App (optional)

Ein Schieberegler 10–100 % sendet `$SPEED,<n>\n` (nur bei Änderung, nicht dauernd).

---

## 7. Sicherheitsverhalten des Roboters (die App muss es nicht nachbauen, aber anzeigen können)

| Situation | Verhalten |
|---|---|
| BLE-Verbindung bricht ab | Sofort Stopp (auch Linienfolge), Format zurück auf Text, Heartbeat aus. Ein aktiver Not-Halt bleibt bestehen |
| Not-Halt (`$ESTOP`) | Aktiv bremsen, alle Fahrbefehle gesperrt, bis `$START` |
| Heartbeat bleibt aus (> 1,5 s) | Alles anhalten + `{"t":"hbtimeout"}` |
| Keine `$J`-Befehle für 300 ms | Stopp + `{"t":"timeout"}` |
| Hindernis vorne < 10 cm | Vorwärts wird stufenlos langsamer (sichtbar an `pwm`) |
| Hindernis am Bremspunkt (~5 cm + Bremsweg) | Bremsung + `{"t":"obstacle",…}`; vorwärts bleibt gesperrt, **rückwärts und drehen gehen weiter** |
| Taste `w/a/s/d/x/b` (z. B. vom PC) | Übernimmt und beendet den Joystick |

**Hinweis für die App:** Bei `obstacle` dem Nutzer etwas anzeigen (z. B. „Hindernis!“). Solange der Joystick nach vorn zeigt, bleibt der Roboter stehen; zurückziehen oder drehen funktioniert.

---

## 8. Empfohlener Ablauf in der App

1. Bluetooth-Berechtigung prüfen/anfragen.
2. Scannen (Filter: Service `6E400001-B5A3-F393-E0A9-E50E24DCCA9E`, sonst Name `Auto-Roboter`).
3. Verbinden.
4. (Android) MTU auf 185 anfordern.
5. Services entdecken → NUS → RX (`…0002`) und TX (`…0003`) holen.
6. **Notifications auf TX aktivieren.**
7. `$FMT,JSON\n` senden.
8. `$INFO\n` senden → `info`-Antwort prüfen (`proto` = 2), `selected` und `estop` übernehmen (der Pico ist die Wahrheit).
9. Heartbeat starten: `$HB\n` alle 500 ms.
10. Ab jetzt kommen `status`-Zeilen 5×/s → UI aktualisieren.
11. Controller nach Abschnitt 4.3 umsetzen, Joystick nach Abschnitt 6.
12. Bei Verbindungsverlust: UI auf „getrennt“, automatisch neu scannen/verbinden, danach wieder ab Schritt 4.

### Beispiel-Sitzung

```
App → $FMT,JSON
Rob ← {"t":"fmt","value":"JSON"}
App → $INFO
Rob ← {"t":"info","name":"Auto-Roboter","proto":1,"maxSpeed":70,"joyTimeoutMs":300}
Rob ← {"t":"status","ms":5021,"mode":"idle","line":[0,0,0],"dist":0.842,"pwm":[0.0,0.0],"speed":[0.000,0.000],"pulses":[0,0],"err":0.0,"trim":0.0,"maxSpeed":70}
App → $J,0,80        (alle 80 ms wiederholt, solange gedrückt)
Rob ← {"t":"status",…,"mode":"joystick","pwm":[56.0,56.0],"speed":[0.240,0.236],…}
App → $J,0,0         (losgelassen)
Rob ← {"t":"status",…,"mode":"idle",…}
```

---

## 9. Ohne eigene App testen

- **nRF Connect** (iOS/Android): Gerät `Auto-Roboter` verbinden → Nordic UART Service → TX-Notify einschalten → auf RX z. B. `$PING` (als Text/UTF-8) schreiben.
- **Bluefruit Connect** (iOS/Android): Verbinden → *UART* zum Mitlesen/Senden, *Controller → Control Pad* zum Fahren.
- **USB-Kabel + seriellen Monitor (115200 Baud):** Alle Befehle funktionieren dort ebenfalls; die Ausgabe ist immer Text.

---

## 10. Kurzreferenz

```
Service  6E400001-B5A3-F393-E0A9-E50E24DCCA9E   (Nordic UART)
RX       6E400002-B5A3-F393-E0A9-E50E24DCCA9E   write / write without response   App → Roboter
TX       6E400003-B5A3-F393-E0A9-E50E24DCCA9E   notify                           Roboter → App
Name     Auto-Roboter        Kein Pairing        1 Verbindung        UTF-8, Zeilen mit \n

$FMT,JSON      JSON-Ausgabe einschalten (nach jedem Verbinden)
$HB            Heartbeat alle 500 ms (Timeout 1,5 s)
$J,x,y         Joystick, x/y −100..100, alle 50–100 ms solange gedrückt, Timeout 300 ms (nur Modus Manuell)
$J,0,0         loslassen
$SPEED,n       Höchsttempo Joystick 10..100 %
$MODE,MANUAL|LINE   Modus (L1/R1)
$START         X: Not-Halt lösen bzw. Linienfolge starten
$ESTOP         Dreieck: Not-Halt mit Sperre
$STOP          Kreis: Aus (ausrollen)
$LINESPEED,n   $LINEKP,v   $BIAS,v   $TRIMRESET
$BRAKE $PING $INFO
```
