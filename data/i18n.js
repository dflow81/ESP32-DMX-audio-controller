// Language of the web interface: German (original) or English.
// The pages are written in German; for English the texts are translated when
// displayed – including everything scripts insert later (MutationObserver), as well as
// alert()/confirm(). Each browser remembers the choice (localStorage).
(function () {
    "use strict";

    const EN = {
        // --- Menu & general ---
        "WLAN": "Wi-Fi", "Lauflicht": "Chases", "Speichern": "Save", "Löschen": "Delete", "Testen": "Test",
        "Anzeigen": "Show", "Übernehmen": "Apply", "Pause": "Pause", "Weiter": "Resume", "aus": "off", "nie": "never",
        "Min": "min", "Name": "Name", "Typ": "Type", "Aktion": "Action", "Gruppe": "Group", "Funktion": "Function",
        "Benutzer": "User", "Passwort": "Password", "Port": "Port", "Topic": "Topic", "Rolle": "Role",
        "Verbunden": "Connected", "unverändert": "unchanged", "Lade …": "Loading …", "–": "–",
        "ESP32 DMX Controller": "ESP32 DMX Controller", "für": "for", "s": "s", "ms": "ms",
        "gedrückt halten": "hold", "Fehler": "Error", "Fehler beim Speichern.": "Error while saving.",
        "Fehler beim Löschen.": "Error while deleting.", "Verbindung abgebrochen.": "Connection lost.",
        "Keine Verbindung …": "No connection …",

        // --- Home page: status ---
        "Live Audio Status": "Live audio status", "Beat": "Beat", "Drop": "Drop", "BPM": "BPM",
        "PC-Steuerung aktiv (Art-Net/sACN) – Effekte pausiert": "PC control active (Art-Net/sACN) – effects paused",
        "Standby – keine Musik erkannt": "Standby – no music detected",
        "Zeitplan: Licht ist gerade aus": "Schedule: lights are currently off",
        "Jetzt einschalten": "Turn on now", "TAP": "TAP", "BPM fest": "Lock BPM",
        "Sync: Master – sendet an andere Controller": "Sync: master – sending to other controllers",
        "Sync: Slave – folgt dem Master": "Sync: slave – following the master",
        "Sync: Slave – kein Master gefunden": "Sync: slave – no master found",

        // --- Effects, master, live buttons ---
        "Effekt-Bibliothek": "Effect library", "Master": "Master", "STROBE": "STROBE", "BLACKOUT": "BLACKOUT",
        "NEBEL": "FOG", "Tippen: Lampe blinkt 5 s": "Tap: lamp flashes for 5 s",
        "Noch keine Nebelmaschine eingerichtet.\nDen DMX-Kanal stellst du unter DMX → Nebelmaschine ein.":
            "No fog machine set up yet.\nSet its DMX channel under DMX → Fog machine.",

        // --- Presets ---
        "Presets": "Presets", "Name für aktuelle Einstellungen": "Name for current settings",
        "Speichert Effekt, Farbe, Speed und Master. Gleicher Name überschreibt.":
            "Saves effect, color, speed and master. Same name overwrites.",
        "Noch keine Presets gespeichert.": "No presets saved yet.", "Bitte einen Namen eingeben.": "Please enter a name.",
        "egal": "any", "ruhig": "calm", "mittel": "medium", "wild": "wild",

        // --- Base color, tempo ---
        "Grundfarbe": "Base color", "Tempo & Übergang": "Tempo & transition", "Effekt Speed": "Effect speed",
        "Intervall:": "Interval:", "Im Takt": "On beat", "½ Beat pro Schritt": "½ beat per step",
        "1 Beat pro Schritt": "1 beat per step", "2 Beats pro Schritt": "2 beats per step",
        "Übergang": "Transition", "Dauer:": "Duration:", "hart": "instant", "Breite": "Width",
        "(Chase, Bounce: leuchtende Lampen)": "(Chase, Bounce: lit lamps)", "Lampen:": "Lamps:",
        "Intensität": "Intensity", "(Flackern, Dichte, Wellenabstand, Blitzlänge, Nachleuchten)":
            "(flicker, density, wave spacing, flash length, afterglow)", "Wert:": "Value:",

        // --- Audio & drop ---
        "Audio & Drop": "Audio & drop", "Helligkeit folgt Lautstärke": "Brightness follows volume",
        "leicht": "light", "stark": "strong", "voll": "full", "Audio Noise Gate": "Audio noise gate",
        "Schwelle:": "Threshold:", "(% der eingemessenen Schwelle, 100 = eingemessen)":
            "(% of the calibrated threshold, 100 = calibrated)", "Mikrofon einmessen …": "Calibrate microphone …",
        "1. Musik aus, Raum ruhig:": "1. Music off, room quiet:", "Stille messen (3 s)": "Measure silence (3 s)",
        "2. Musik so laut wie auf der Party:": "2. Music as loud as at the party:",
        "Musik messen (8 s)": "Measure music (8 s)", "3.": "3.", "Messe … bitte warten": "Measuring … please wait",
        "Messung fertig.": "Measurement done.", "Bitte zuerst Stille und/oder Musik messen":
            "Please measure silence and/or music first",
        "Drop-Erkennung": "Drop detection", "Bei Drop:": "On drop:", "Strobe": "Strobe",
        "Weißer Blitz": "White flash", "Farbexplosion": "Color burst", "Preset aufrufen": "Recall preset",
        "Nebelstoß bei Drop": "Fog burst on drop",
        "Zuerst ein Preset speichern, das beim Drop aufgerufen werden soll.":
            "First save a preset to recall on drop.",

        // --- Palette, groups ---
        "Farbpalette": "Color palette", "Eigene Farben für Chase, Disco, Fade & Co.":
            "Own colors for chase, disco, fade & co.",
        "2 Farben": "2 colors", "3 Farben": "3 colors", "4 Farben": "4 colors", "5 Farben": "5 colors",
        "6 Farben": "6 colors", "7 Farben": "7 colors", "8 Farben": "8 colors", "Gruppen": "Groups",
        "Lampen ordnest du in der DMX-Config einer Gruppe zu. Ohne eigenen Effekt folgt eine Gruppe dem Haupteffekt.":
            "Assign lamps to a group in the DMX config. Without its own effect a group follows the main effect.",
        "eigener Effekt": "own effect",

        // --- Automation ---
        "Automatik (Auto-Show, Standby)": "Automation (auto show, standby)",
        "Auto-Show: Presets durchschalten alle": "Auto show: cycle presets every",
        "16 Beats": "16 beats", "32 Beats": "32 beats", "64 Beats": "64 beats", "128 Beats": "128 beats",
        "nach Musik-Energie": "by music energy", "Musik gerade:": "Music now:",
        ". Presets mit ⚡ als ruhig/mittel/wild markieren.": ". Mark presets with ⚡ as calm/medium/wild.",
        "Standby nach": "Standby after", "5 Min": "5 min", "10 Min": "10 min", "15 Min": "15 min",
        "30 Min": "30 min", "60 Min": "60 min", "ohne Musik:": "without music:", "nur dimmen (20 %)": "dim only (20 %)",

        // --- Cue list ---
        "Cue-Liste": "Cue list", "+ Cue": "+ Cue", "Liste speichern": "Save list", "Endlos wiederholen": "Loop",
        "▶ Start": "▶ Start", "⏭ Nächster": "⏭ Next", "■ Stopp": "■ Stop",
        "Jeder Cue ruft ein Preset auf und läuft die eingestellte Zeit. Start beendet die Auto-Show.":
            "Each cue recalls a preset and runs for the set time. Start ends the auto show.",
        "Noch keine Cues. Erst Presets speichern, dann hier mit „+ Cue“ hinzufügen.":
            "No cues yet. Save presets first, then add them here with “+ Cue”.",
        "Beats": "Beats", "Sek.": "sec.", "Bitte zuerst Presets speichern.": "Please save presets first.",
        "Maximal 32 Cues.": "Maximum 32 cues.", "Cue-Liste gespeichert.": "Cue list saved.",
        "Die Cue-Liste ist leer.": "The cue list is empty.",

        // --- Virtual stage ---
        "Virtuelle Bühne": "Virtual stage",
        "Zeigt live, was an die Lampen gesendet wird (Farbe, Helligkeit, bei Moving Heads die Position).":
            "Shows live what is sent to the lamps (color, brightness, position for moving heads).",
        "Noch keine Lampen angelegt (DMX-Config).": "No lamps set up yet (DMX config).",

        // --- Wi-Fi page ---
        "WLAN Konfiguration": "Wi-Fi configuration", "WLAN Netzwerke": "Wi-Fi networks",
        "Scanne nach verfügbaren Netzwerken in der Umgebung.": "Scan for available networks nearby.",
        "Netzwerke scannen": "Scan networks", "Suche läuft...": "Scanning...",
        "Scan wird durchgeführt...": "Scanning...", "Fehler beim Scannen.": "Error while scanning.",
        "Keine Netzwerke gefunden.": "No networks found.", "Verbindung herstellen": "Connect",
        "Netzwerkname (SSID)": "Network name (SSID)", "Netzwerk wählen oder tippen": "Choose or type a network",
        "Passwort eingeben": "Enter password", "Einstellungen speichern": "Save settings",
        "Gefahrenzone": "Danger zone",
        "Löscht die gespeicherten WLAN-Daten. Der Controller startet danach im Access-Point-Modus neu.":
            "Deletes the saved Wi-Fi credentials. The controller then restarts in access point mode.",
        "Gespeichertes WLAN löschen": "Delete saved Wi-Fi", "Bitte wählen Sie ein WLAN!": "Please choose a Wi-Fi network!",
        "Gespeichert! Controller startet neu.": "Saved! Controller is restarting.",
        "WLAN-Daten wirklich löschen? Der Controller ist danach nur noch über seinen eigenen Hotspot erreichbar.":
            "Really delete the Wi-Fi credentials? The controller will then only be reachable via its own hotspot.",
        "WLAN gelöscht! Der Controller startet jetzt neu.": "Wi-Fi deleted! The controller is restarting now.",
        "Zugang (Passwort)": "Access (password)", "Bedienung nur mit Passwort": "Require a password",
        "Passwort (mind. 4 Zeichen)": "Password (min. 4 characters)", "Zugang speichern": "Save access",
        "Schützt alle Seiten und Funktionen. Nach dem Einschalten fragt der Browser einmal nach Benutzer und Passwort. Update und Backup nutzen dann ebenfalls dieses Passwort.":
            "Protects all pages and functions. After enabling, the browser asks once for user and password. Update and backup then use this password too.",
        "Passwort vergessen?": "Forgot the password?",
        "Beim Einschalten des Controllers den Tap-Taster (GPIO 14) gedrückt halten – dann ist der Passwortschutz wieder aus.":
            "Hold the tap button (GPIO 14) while powering on the controller – this turns the password protection off.",
        "Das Passwort muss mindestens 4 Zeichen haben.": "The password must have at least 4 characters.",
        "Bitte zuerst ein Passwort eingeben.": "Please enter a password first.",
        "Passwortschutz ist an. Der Browser fragt gleich nach Benutzer und Passwort.":
            "Password protection is on. The browser will ask for user and password.",
        "Passwortschutz ist aus.": "Password protection is off.",
        "noch keins gesetzt": "none set yet", "kein Passwort": "no password",
        "Mehrere Controller synchron": "Synchronize several controllers",
        "Master (gibt den Takt vor)": "Master (sets the beat)", "Slave (folgt dem Master)": "Slave (follows the master)",
        "Per ESP-NOW (direkt von ESP zu ESP). Der Master sendet Beat, Tempo, Effekt, Farben, Palette, Blackout, Strobe und Drops; Slaves übernehmen das für ihre eigenen Lampen. Alle Controller müssen im":
            "Via ESP-NOW (directly from ESP to ESP). The master sends beat, tempo, effect, colors, palette, blackout, strobe and drops; slaves apply them to their own lamps. All controllers must be on the",
        "selben WLAN": "same Wi-Fi",
        "sein und die gleiche Gruppe haben. Fällt der Master aus, arbeitet ein Slave nach 1,5 s wieder selbstständig.":
            "and use the same group. If the master fails, a slave works on its own again after 1.5 s.",
        "MQTT / Home Assistant": "MQTT / Home Assistant", "MQTT verwenden": "Use MQTT",
        "Broker (IP/Name)": "Broker (IP/name)", "z. B. 192.168.178.10": "e.g. 192.168.178.10",
        "MQTT speichern": "Save MQTT", "Status:": "Status:",
        "Home Assistant findet den Controller automatisch (MQTT-Discovery): als Licht (an/aus = Blackout, Helligkeit = Master, Farbe, Effekte), Preset-Auswahl und BPM-Sensor. Voraussetzung ist ein MQTT-Broker, z. B. das Mosquitto-Add-on in Home Assistant.":
            "Home Assistant finds the controller automatically (MQTT discovery): as a light (on/off = blackout, brightness = master, color, effects), a preset selector and a BPM sensor. Requires an MQTT broker, e.g. the Mosquitto add-on in Home Assistant.",
        "MQTT gespeichert – der Controller verbindet sich neu.": "MQTT saved – the controller reconnects.",
        "verbunden ✓": "connected ✓", "nicht verbunden": "not connected",

        // --- DMX config ---
        "DMX Konfiguration": "DMX configuration", "Fixtures": "Fixtures", "Start": "Start",
        "Bereich (Belegt)": "Range (used)", "Fixture hinzufügen": "Add fixture", "Name (z.B. Spot 1)": "Name (e.g. Spot 1)",
        "+ Hinzufügen": "+ Add", "Finden": "Find", "Lampe blinkt 5 s": "Lamp flashes for 5 s",
        "Fixture-Typen": "Fixture types", "Kanäle": "Channels",
        "Offsets (Dim,R,G,B,W,Pan,Tilt | Gobo,Farbrad,Prisma,Fokus)": "Offsets (dim,R,G,B,W,pan,tilt | gobo,color wheel,prism,focus)",
        "Neuen Typ hinzufügen": "Add new type", "Vorlage:": "Template:",
        "Typische Kanalbelegungen – bitte mit der Anleitung deiner Lampe vergleichen und bei Bedarf anpassen. Offsets zählen ab 0.":
            "Typical channel layouts – please compare with your lamp's manual and adjust if needed. Offsets start at 0.",
        "– eigene Belegung –": "– custom layout –", "Typname": "Type name", "Dimmer": "Dimmer", "Pan": "Pan", "Tilt": "Tilt",
        "Gobo": "Gobo", "Farbrad": "Color wheel", "Prisma": "Prism", "Fokus": "Focus", "Typ hinzufügen": "Add type",
        "Offsets zählen ab 0 (erster Kanal des Geräts = 0). Leer lassen, wenn das Gerät den Kanal nicht hat.":
            "Offsets start at 0 (first channel of the device = 0). Leave empty if the device lacks the channel.",
        "Konfiguration speichern": "Save configuration", "Konfiguration LIVE übernommen!": "Configuration applied LIVE!",
        "Zuerst einen Fixture-Typ erstellen!": "Create a fixture type first!",
        "RGB, 3 Kanäle (R, G, B)": "RGB, 3 channels (R, G, B)", "RGBW, 4 Kanäle (R, G, B, W)": "RGBW, 4 channels (R, G, B, W)",
        "Dimmer + RGB, 4 Kanäle": "Dimmer + RGB, 4 channels", "RGB + Dimmer, 4 Kanäle": "RGB + dimmer, 4 channels",
        "Dimmer + RGBW, 5 Kanäle": "Dimmer + RGBW, 5 channels",
        "PAR 6 Kanäle (Dimmer, R, G, B, Strobe, Modus)": "PAR 6 channels (dimmer, R, G, B, strobe, mode)",
        "PAR 7 Kanäle (Dimmer, R, G, B, W, Strobe, Modus)": "PAR 7 channels (dimmer, R, G, B, W, strobe, mode)",
        "PAR 8 Kanäle (Dimmer, R, G, B, W, Amber, UV, Strobe)": "PAR 8 channels (dimmer, R, G, B, W, amber, UV, strobe)",
        "Moving Head Spot 9 Kanäle (Pan, Tilt, Farbrad, Gobo, Strobe, Dimmer, Speed, Auto, Reset)":
            "Moving head spot 9 channels (pan, tilt, color wheel, gobo, strobe, dimmer, speed, auto, reset)",
        "Moving Head Spot 11 Kanäle (Pan, Tilt, Pan fein, Tilt fein, Speed, Farbrad, Gobo, Strobe, Dimmer, Prisma, Fokus)":
            "Moving head spot 11 channels (pan, tilt, pan fine, tilt fine, speed, color wheel, gobo, strobe, dimmer, prism, focus)",
        "Moving Head Wash 14 Kanäle (Pan, Pan fein, Tilt, Tilt fein, Speed, Dimmer, Strobe, R, G, B, W, …)":
            "Moving head wash 14 channels (pan, pan fine, tilt, tilt fine, speed, dimmer, strobe, R, G, B, W, …)",
        "Nebelmaschine": "Fog machine", "DMX-Kanal": "DMX channel", "Stärke (0–255)": "Strength (0–255)",
        "Stoß bei Drop": "Burst on drop", "Automatisch alle": "Automatically every",
        "Kanal 0 = keine Nebelmaschine. Den Kanal nicht für eine Lampe verwenden. „Alle 0 Min“ = kein automatischer Nebel. Ob bei Drops genebelt wird, stellst du auf der Startseite ein. Der Nebelkanal wirkt unabhängig von Master und Blackout.":
            "Channel 0 = no fog machine. Do not use the channel for a lamp. “Every 0 min” = no automatic fog. Whether to fog on drops is set on the home page. The fog channel works independently of master and blackout.",
        "Zeitplan": "Schedule", "Licht nur zu bestimmten Zeiten": "Lights only at certain times", "an ab": "on from",
        "aus ab": "off from", "beim Einschalten": "when turning on", "– keins –": "– none –",
        "Uhrzeit des Controllers:": "Controller time:", "noch keine (kein Internet?)": "none yet (no internet?)",
        ". Die Zeit kommt aus dem Internet (WLAN nötig); ohne Uhrzeit bleibt das Licht an. Außerhalb der Zeit ist alles dunkel – auf der Startseite lässt es sich mit „Jetzt einschalten“ bis zum nächsten Wechsel überstimmen.":
            ". The time comes from the internet (Wi-Fi needed); without a time the lights stay on. Outside the time everything is dark – on the home page “Turn on now” overrides this until the next change.",
        "Display": "Display", "Ausschalten nach": "Turn off after", "ohne Musik und Bedienung": "without music or interaction",
        "Schützt das OLED vor dem Einbrennen. Nach der halben Zeit wird es gedimmt; Musik, Bedienung oder ein Taster wecken es wieder.":
            "Protects the OLED from burn-in. After half the time it is dimmed; music, interaction or a button wakes it up.",
        "Taster / Fußschalter": "Buttons / footswitch", "GPIO": "GPIO", "Strobe (halten)": "Strobe (hold)",
        "Blackout an/aus": "Blackout on/off", "Nebel (halten)": "Fog (hold)", "Tap-Tempo": "Tap tempo",
        "Nächstes Preset": "Next preset",
        "Jeden Taster zwischen GPIO und GND anschließen (keine Widerstände nötig). Die Belegung steht in":
            "Connect each button between GPIO and GND (no resistors needed). The mapping is in",
        "PC-Steuerung (Art-Net / sACN)": "PC control (Art-Net / sACN)",
        "Steuerung per Art-Net / sACN erlauben": "Allow control via Art-Net / sACN", "Universum:": "Universe:",
        "In der Lichtsoftware (z. B. QLC+) einen Art-Net- oder E1.31-Ausgang auf":
            "In the lighting software (e.g. QLC+) set an Art-Net or E1.31 output to",
        "diese IP": "this IP",
        "und das gleiche Universum einstellen. Solange Daten kommen, steuert der PC die Lampen (nur Blackout wirkt noch); 3 s nach dem letzten Paket übernehmen wieder die Effekte.":
            "and the same universe. While data arrives, the PC controls the lamps (only blackout still works); 3 s after the last packet the effects take over again.",
        "Universum 1 = Art-Net-Universum 0 bzw. sACN-Universum 1 (wie in QLC+ angezeigt).":
            "Universe 1 = Art-Net universe 0 or sACN universe 1 (as shown in QLC+).",

        // --- Moving head ---
        "MH Control Center": "MH control center", "Moving Head Setup": "Moving head setup",
        "-- Head auswählen --": "-- Select head --", "PUNKT A (Start)": "POINT A (start)", "PUNKT B (Ende)": "POINT B (end)",
        "MOTOR-TEST STARTEN": "START MOTOR TEST", "TEST LÄUFT... (STOPP)": "TEST RUNNING... (STOP)",
        "BPM AUTO START/STOP": "BPM AUTO START/STOP", "Bewegung bei BPM Auto (alle Heads)": "Movement with BPM auto (all heads)",
        "Kreis (springt im Takt)": "Circle (jumps on beat)", "Liegende Acht": "Figure eight",
        "Schwenk links/rechts": "Pan left/right", "Nicken hoch/runter": "Tilt up/down", "Zufällige Positionen": "Random positions",
        "Heads versetzt (Welle)": "Heads offset (wave)", "Positionen (alle Heads)": "Positions (all heads)",
        "Name, z. B. Bühne": "Name, e.g. stage",
        "Speichert die aktuelle Position aller Moving Heads. Anfahren schaltet BPM Auto für diese Heads aus.":
            "Saves the current position of all moving heads. Recalling turns off BPM auto for these heads.",
        "Noch keine Positionen gespeichert.": "No positions saved yet.", "Zusatzkanäle (gewählter Head)": "Extra channels (selected head)",
        "Bitte erst ein Gerät wählen!": "Please select a device first!", "Fehler beim Laden!": "Error while loading!",
        "Fehler beim Auswählen!": "Error while selecting!", "Fehler beim Senden!": "Error while sending!",
        "Bewegung gespeichert": "Movement saved", "Position gespeichert": "Position saved",
        "Test-Modus: Disco-Bewegung aktiv": "Test mode: disco movement active", "Test beendet.": "Test finished.",
        "BPM Auto AN": "BPM auto ON", "BPM Auto AUS": "BPM auto OFF", "Keine Moving Heads angelegt": "No moving heads set up",

        // --- Monitor ---
        "DMX Monitor": "DMX monitor", "DMX-Monitor": "DMX monitor", "Auslastung: …": "Load: …", "Rot": "Red", "Grün": "Green",
        "Blau": "Blue", "Weiß": "White", "Pan/Tilt": "Pan/tilt", "Gobo/Farbrad/Prisma/Fokus": "Gobo/color wheel/prism/focus",
        "sonstige": "other", "Weitere Kanäle": "Further channels", "ohne Fixture (z. B. von Art-Net)": "without fixture (e.g. from Art-Net)",
        "Kanaltest": "Channel test", "Ab Kanal": "From channel", "Alle zurücksetzen": "Reset all",
        "Setzt einzelne DMX-Kanäle von Hand – gut, um eine neue Lampe auszuprobieren, bevor ihr Typ angelegt ist. Die Werte haben Vorrang vor den Effekten und enden 2 Minuten nach der letzten Änderung oder mit „Alle zurücksetzen“.":
            "Sets individual DMX channels by hand – handy for trying out a new lamp before its type exists. The values override the effects and end 2 minutes after the last change or with “Reset all”.",

        // --- Chases ---
        "Lauflicht-Editor": "Chase editor", "Eigene Lauflichter": "Own chases", "Name, z. B. Polizei": "Name, e.g. police",
        "Lampen": "Lamps", "Schritte": "Steps", "weich überblenden": "smooth crossfade", "Pinsel:": "Brush:",
        "Zeile = Schritt, Spalte = Lampe. Tippen oder darüberwischen malt mit dem Pinsel. Gibt es mehr Lampen als Spalten, wiederholt sich das Muster. Tempo: Speed-Regler bzw. „Im Takt“.":
            "Row = step, column = lamp. Tap or swipe to paint with the brush. With more lamps than columns the pattern repeats. Tempo: speed slider or “On beat”.",
        "Lauflicht-Muster": "Chase pattern", "Alles schwarz": "All black", "Dieses Lauflicht löschen?": "Delete this chase?",
        "Fehler beim Speichern": "Error while saving",

        // --- Update / backup ---
        "Update - ESP32 DMX Controller": "Update - ESP32 DMX Controller", "Firmware": "Firmware",
        "Datei: .pio/build/esp32dev/firmware.bin": "File: .pio/build/esp32dev/firmware.bin", "Firmware hochladen": "Upload firmware",
        "Dateisystem (Web-Oberfläche)": "File system (web interface)",
        "Datei: .pio/build/esp32dev/littlefs.bin (erstellen mit \"Build Filesystem Image\"). Deine Einstellungen (Fixtures, WLAN) bleiben erhalten.":
            "File: .pio/build/esp32dev/littlefs.bin (create with \"Build Filesystem Image\"). Your settings (fixtures, Wi-Fi) are kept.",
        "Dateisystem hochladen": "Upload file system", "Einstellungen sichern": "Back up settings",
        "Fixtures, Typen, Presets und alle Einstellungen als Datei. Das WLAN-Passwort ist nicht enthalten und bleibt beim Wiederherstellen erhalten.":
            "Fixtures, types, presets and all settings as a file. The Wi-Fi password is not included and is kept when restoring.",
        "Backup herunterladen": "Download backup", "Backup wiederherstellen": "Restore backup",
        "Bitte zuerst eine .bin-Datei wählen.": "Please choose a .bin file first.",
        "Bitte zuerst eine Backup-Datei wählen.": "Please choose a backup file first.",
        "Alle aktuellen Einstellungen durch das Backup ersetzen?": "Replace all current settings with the backup?",
        "Backup wiederhergestellt. Neustart … die Seite lädt gleich neu.": "Backup restored. Restarting … the page reloads shortly.",
        "Update erfolgreich. Neustart … die Seite lädt gleich neu.": "Update successful. Restarting … the page reloads shortly.",

        // --- Messages from the controller ---
        "Name fehlt oder ist zu lang": "Name missing or too long", "Body zu groß": "Request too large",
        "Kein Speicher": "Out of memory", "Datei ist kein gültiges JSON": "File is not valid JSON",
        "Datei ist kein Backup dieses Controllers": "File is not a backup of this controller",
        "Speichern fehlgeschlagen": "Saving failed", "Ungültige SSID": "Invalid SSID", "JSON Error": "JSON error"
    };

    // Texts with inserted values
    const PATTERNS = [
        [/^läuft – Cue (\d+) von (\d+)$/, "running – cue $1 of $2"],
        [/^1 Lampe$/, "1 lamp"],
        [/^(\d+) Lampen$/, "$1 lamps"],
        [/^Kanal (\d+)$/, "Channel $1"],
        [/^Gruppe (\d+)$/, "Group $1"],
        [/^Platz (\d+): \(frei\)$/, "Slot $1: (free)"],
        [/^Platz (\d+): (.*)$/, "Slot $1: $2"],
        [/^(.*) · Kanal (\d+)–(\d+)$/, "$1 · channel $2–$3"],
        [/^(\d+) Kanäle werden gesendet · (.*)$/, "$1 channels being sent · $2"],
        [/^Hochladen … (\d+) %$/, "Uploading … $1 %"],
        [/^Fehler: (.*)$/, (m, x) => "Error: " + tr(x)],
        [/^Gespeichert – erscheint als ★ (.*) in der Effekt-Bibliothek\.$/, "Saved – appears as ★ $1 in the effect library."],
        [/^Befehl gesendet: (.*)$/, "Command sent: $1"],
        [/^(.*): BPM Auto AN$/, "$1: BPM auto ON"],
        [/^(.*): BPM Auto AUS$/, "$1: BPM auto OFF"],
        [/^Energie: (\S+) \(tippen zum Ändern\)$/, (m, x) => "Energy: " + tr(x) + " (tap to change)"],
        [/^Preset "(.*)" löschen\?$/, 'Delete preset "$1"?'],
        [/^Position "(.*)" löschen\?$/, 'Delete position "$1"?'],
        [/^Maximal (\d+) Presets$/, "Maximum $1 presets"],
        [/^Maximal (\d+) Positionen$/, "Maximum $1 positions"],
        [/^Eingemessen \((.*)\)$/, (m, x) => "Calibrated (" + x.replace("Stille", "silence").replace("Musik", "music") + ")"],
        [/^Eingemessen$/, "Calibrated"],
        [/^(.*) \(fehlt\)$/, "$1 (missing)"],
        [/^Position: (.*)$/, "Position: $1"],
        [/^(\d+(?:,\d+)?) s$/, (m, x) => x.replace(",", ".") + " s"],
        // Load line on the monitor page
        [/^(Core 0 – Audio:.*)$/, (m, s) => s.replace("/Frame", "/frame").replace("Display:", "display:")
            .replace("Effekte:", "effects:").replace("Bilder/s", "frames/s").replace("senden", "to send")
            .replace("RAM frei:", "RAM free:")]
    ];

    // Normalize keys like the looked-up texts (spaces/line breaks → one space),
    // line breaks in the translation are kept
    const DICT = {};
    for (const k of Object.keys(EN)) DICT[k.replace(/\s+/g, " ").trim()] = EN[k];

    function getLang() {
        try {
            const l = localStorage.getItem("lang");
            if (l === "de" || l === "en") return l;
        } catch (e) {}
        return (navigator.language || "de").toLowerCase().startsWith("de") ? "de" : "en";
    }
    const LANG = getLang();
    window.LANG = LANG;

    // Translate one (trimmed) text; unknown texts stay unchanged
    function tr(text) {
        const key = text.replace(/\s+/g, " ").trim();
        if (!key) return text;
        if (Object.prototype.hasOwnProperty.call(DICT, key)) return DICT[key];
        for (const [re, rep] of PATTERNS) {
            if (re.test(key)) return key.replace(re, rep);
        }
        return text;
    }

    // For scripts: t("…") returns the translation (the original text in German)
    window.t = s => (LANG === "en" && s != null) ? keepSpaces(String(s)) : s;

    function keepSpaces(s) {
        const out = tr(s);
        if (out === s) return s;
        const lead = s.match(/^\s*/)[0], trail = s.match(/\s*$/)[0];
        return lead + out + trail;
    }

    function translateNode(node) {
        if (node.nodeType === 3) {
            const v = keepSpaces(node.nodeValue);
            if (v !== node.nodeValue) node.nodeValue = v;
            return;
        }
        if (node.nodeType !== 1 || node.tagName === "SCRIPT" || node.tagName === "STYLE") return;
        for (const a of ["placeholder", "title", "alt"]) {
            if (node.hasAttribute(a)) {
                const v = keepSpaces(node.getAttribute(a));
                if (v !== node.getAttribute(a)) node.setAttribute(a, v);
            }
        }
        node.childNodes.forEach(translateNode);
    }

    // DE | EN switch on the right of the nav bar
    function addSwitch() {
        const nav = document.querySelector(".nav");
        if (!nav) return;
        const sw = document.createElement("span");
        sw.className = "lang-switch";
        ["de", "en"].forEach((l, i) => {
            if (i) sw.appendChild(document.createTextNode(" | "));
            const a = document.createElement("a");
            a.href = "#";
            a.textContent = l.toUpperCase();
            if (l === LANG) a.className = "active";
            a.onclick = e => {
                e.preventDefault();
                try { localStorage.setItem("lang", l); } catch (err) {}
                // Also report it to the controller (display language), then reload
                fetch("/api/settings?lang=" + l).catch(() => {}).finally(() => location.reload());
            };
            sw.appendChild(a);
        });
        nav.appendChild(sw);
    }

    function start() {
        document.documentElement.lang = LANG;
        addSwitch();
        if (LANG !== "en") return;
        document.title = keepSpaces(document.title);
        translateNode(document.body);
        new MutationObserver(muts => {
            for (const m of muts) {
                if (m.type === "characterData") translateNode(m.target);
                m.addedNodes.forEach(translateNode);
            }
        }).observe(document.body, { childList: true, subtree: true, characterData: true });
    }

    if (LANG === "en") {
        const _alert = window.alert.bind(window), _confirm = window.confirm.bind(window);
        window.alert = m => _alert(window.t(m));
        window.confirm = m => _confirm(window.t(m));
    }

    if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", start);
    else start();
})();
