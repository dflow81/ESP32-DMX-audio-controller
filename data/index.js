let isDragging = false;
const baseEffects = [
    "static", "fade", "rainbow", "chase", "bounce",
    "dualwave", "pulse", "sparkle", "strobe",
    "fire", "candle", "aurora", "ocean", "sunset",
    "fullstrobe", "randomstrobe",
    "dimmerwave", "dimmerchase", "dimmerbounce",
    "disco","disco2", "lightorgan",
    "pump", "snareflash", "basswave"
];
let effects = baseEffects.slice();
// Anzeigenamen, wo sie vom internen Namen abweichen (eigene Lauflichter kommen dazu)
const effectLabels = { lightorgan: "light organ", snareflash: "snare flash", basswave: "bass wave" };
const esc = t => String(t).replace(/[&<>"']/g, c => ({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));

// Eigene Lauflichter als Effekte "user1"…"user4" ergänzen
function setUserChases(names) {
    effects = baseEffects.slice();
    (names || []).forEach((n, i) => {
        if (!n) return;
        effects.push("user" + (i + 1));
        effectLabels["user" + (i + 1)] = "★ " + n;
    });
    buildEffectButtons();
    setEffect(currentEffect || "static", false);
}
const effectLabel = e => effectLabels[e] || e;

const canvas = document.getElementById('colorCanvas');
const ctx = canvas.getContext('2d');
const cursor = document.getElementById('colorCursor');
const radius = canvas.width / 2;

// Einklappbare Bereiche: Zustand pro Browser merken
function setupFolds() {
    document.querySelectorAll("details.fold").forEach(d => {
        try {
            const saved = localStorage.getItem("fold:" + d.id);
            if (saved !== null) d.open = saved === "1";
        } catch (e) {}
        d.addEventListener("toggle", () => {
            try { localStorage.setItem("fold:" + d.id, d.open ? "1" : "0"); } catch (e) {}
        });
    });
}

window.onload = () => {
    setupFolds();
    buildEffectButtons();
    setupStrobeButton();
    document.getElementById("tapBtn").addEventListener("pointerdown", e => { e.preventDefault(); tap(); });
    loadConfig();
    loadPresets();
    loadCues();
    setupHoldButton("fogBtn", "/api/fog", () => {
        if (fogConfigured) return true;
        alert("Noch keine Nebelmaschine eingerichtet.\nDen DMX-Kanal stellst du unter DMX → Nebelmaschine ein.");
        return false;
    });
    drawCircle();
    connectLive();

    // Slider-Sperre
    const sliders = document.querySelectorAll('input[type="range"]');
    sliders.forEach(s => {
        ["mousedown", "touchstart"].forEach(e => s.addEventListener(e, () => isDragging = true));
        ["mouseup", "touchend"].forEach(e => s.addEventListener(e, () => isDragging = false));
    });
};

function drawCircle() {
    for (let angle = 0; angle < 360; angle++) {
        const startAngle = (angle - 2) * Math.PI / 180;
        const endAngle = (angle) * Math.PI / 180;
        ctx.beginPath();
        ctx.moveTo(radius, radius);
        ctx.arc(radius, radius, radius, startAngle, endAngle);
        const grad = ctx.createRadialGradient(radius, radius, 0, radius, radius, radius);
        grad.addColorStop(0, 'white');
        grad.addColorStop(1, `hsl(${angle}, 100%, 50%)`);
        ctx.fillStyle = grad;
        ctx.fill();
    }
}

function loadConfig() {
    fetch("/api/getConfig").then(r => r.json()).then(data => {
        if(!isDragging) {
            document.getElementById("master").value = data.master;
            document.getElementById("masterVal").textContent = data.master;
            if(data.timerSpeed) {
                document.getElementById("timerFader").value = data.timerSpeed;
                document.getElementById("timerVal").textContent = data.timerSpeed;
            }
            if(data.dropEnabled !== undefined) {
                document.getElementById("dropEnabled").checked = data.dropEnabled;
            }
            if(data.beatSync !== undefined) {
                document.getElementById("beatSync").checked = data.beatSync;
                document.getElementById("stepsPerBeat").value = String(data.stepsPerBeat);
                updateSyncUi();
            }
            if(data.dropAction !== undefined) {
                document.getElementById("dropAction").value = String(data.dropAction);
                dropPresetName = data.dropPreset || "";
                updateDropUi();
            }
            if(data.palette) showPalette(data.palette);
            if(data.fadeMs !== undefined) {
                document.getElementById("fadeMs").value = data.fadeMs;
                document.getElementById("fadeVal").textContent = fmtFade(data.fadeMs);
                document.getElementById("fxWidth").value = data.fxWidth;
                document.getElementById("widthVal").textContent = data.fxWidth;
                document.getElementById("fxIntensity").value = data.fxIntensity;
                document.getElementById("intensityVal").textContent = data.fxIntensity;
                document.getElementById("levelDim").checked = !!data.levelDim;
                document.getElementById("levelDimDepth").value = String(data.levelDimDepth);
            }
            buildStage(data);
            if (data.userChases) setUserChases(data.userChases);
            if (data.autoShowSmart !== undefined) document.getElementById("autoShowSmart").checked = data.autoShowSmart;
            if(data.groups) showGroups(data.groups, data.fixtures || []);
            if(data.fog) {
                const hasFog = data.fog.channel > 0;
                document.querySelectorAll(".fog-only").forEach(el => el.style.display = hasFog ? "" : "none");
                fogConfigured = hasFog;
                document.getElementById("fogBtn").classList.toggle("unconfigured", !hasFog);
                document.getElementById("fogOnDrop").checked = !!data.fog.onDrop;
            }
            if(data.standbyMin !== undefined) {
                document.getElementById("standbyMin").value = String(data.standbyMin);
                standbyPresetName = data.standbyPreset || "";
                document.getElementById("standbyPreset").value = standbyPresetName;
            }
            if(data.cueLoop !== undefined) document.getElementById("cueLoop").checked = data.cueLoop;
            document.getElementById("calHint").style.display = data.calibrated ? "" : "none";
            if(data.autoShow !== undefined) {
                document.getElementById("autoShow").checked = data.autoShow;
                document.getElementById("showBeats").value = String(data.showBeats);
            }
            if(data.audioThreshold) {
                document.getElementById("threshold").value = data.audioThreshold;
                document.getElementById("thresholdVal").textContent = data.audioThreshold;
            }
        }
        setEffect(data.effect || "static", false);
    });
}

// --- Live-Status: per WebSocket (sofort bei jedem Beat), Polling nur als Notlösung ---
let pollTimer = null;
function connectLive() {
    const ws = new WebSocket(`ws://${location.host}/ws`);
    ws.onopen = () => {
        if (pollTimer) { clearInterval(pollTimer); pollTimer = null; }
    };
    ws.onmessage = e => showStatus(JSON.parse(e.data));
    ws.onclose = () => {
        // Verbindung weg (z. B. Neustart): solange pollen und alle 3 s neu verbinden
        if (!pollTimer) pollTimer = setInterval(updateStatus, 200);
        setTimeout(connectLive, 3000);
    };
}

function updateStatus() {
    fetch("/api/status").then(r => r.json()).then(showStatus).catch(() => {});
}

let shownPreset = -1;
function showStatus(data) {
    // Effekt-Bibliothek und Presets mit dem tatsächlichen Zustand abgleichen
    if (data.effect !== undefined && data.effect !== currentEffect) setEffect(data.effect, false);
    if (data.preset !== undefined && data.preset !== shownPreset) {
        shownPreset = data.preset;
        markActivePreset();
    }
    const levelPercent = Math.min(100, data.level * 100); 
    document.getElementById("levelBar").style.width = levelPercent + "%";
    if (data.beat) {
        const b = document.getElementById("beat");
        b.classList.add("on");
        setTimeout(() => b.classList.remove("on"), 150);
    }
    if (data.drop) {
        const d = document.getElementById("drop");
        d.classList.add("on");
        setTimeout(() => d.classList.remove("on"), 500);
    }
    document.getElementById("bpm").textContent = Math.round(data.bpm);
    if (!isDragging) document.getElementById("masterVal").textContent = data.master;
    if (data.spec) drawSpectrum(data.spec);
    if (data.energy !== undefined) document.getElementById("energyNow").textContent = ENERGY_NAMES[data.energy];
    if (data.sync !== undefined) {
        const sb = document.getElementById("syncBadge");
        let txt = "";
        if (data.sync === 1) txt = "Sync: Master – sendet an andere Controller";
        if (data.sync === 2) txt = data.syncRx ? "Sync: Slave – folgt dem Master" : "Sync: Slave – kein Master gefunden";
        sb.textContent = txt;
        sb.style.display = txt ? "" : "none";
    }
    if (data.scheduleDark !== undefined) {
        document.getElementById("scheduleBadge").style.display = data.scheduleDark ? "" : "none";
    }
    if (data.standby !== undefined) {
        document.getElementById("standbyBadge").style.display = data.standby ? "" : "none";
    }
    if (data.fog !== undefined) document.getElementById("fogBtn").classList.toggle("on", data.fog);
    if (data.cueRunning !== undefined) showCueState(data.cueRunning, data.cueIndex);
    if (data.netActive !== undefined) {
        document.getElementById("netBadge").style.display = data.netActive ? "" : "none";
    }
    if (data.bpmLocked !== undefined) {
        document.getElementById("lockBtn").classList.toggle("on", data.bpmLocked);
    }
    if (data.blackout !== undefined) {
        document.getElementById("blackoutBtn").classList.toggle("on", data.blackout);
    }
}

// --- Tap-Tempo ---
function tap() {
    const btn = document.getElementById("tapBtn");
    btn.classList.add("on");
    setTimeout(() => btn.classList.remove("on"), 100);
    fetch("/api/tap").then(r => r.text()).then(bpm => {
        document.getElementById("bpm").textContent = bpm;
    });
}

function toggleBpmLock() {
    const on = !document.getElementById("lockBtn").classList.contains("on");
    document.getElementById("lockBtn").classList.toggle("on", on);
    fetch("/api/bpmLock?on=" + (on ? 1 : 0));
}

// --- Effekte im Takt ---
function updateSyncUi() {
    const sync = document.getElementById("beatSync").checked;
    document.getElementById("timerFader").disabled = sync;
    document.getElementById("stepsPerBeat").disabled = !sync;
}

function saveBeatSync() {
    updateSyncUi();
    const sync = document.getElementById("beatSync").checked ? 1 : 0;
    const div = document.getElementById("stepsPerBeat").value;
    fetch(`/api/settings?beatSync=${sync}&stepsPerBeat=${div}`);
}

// --- Drop-Reaktion ---
let dropPresetName = "";
// Alle Preset-Auswahlen (Drop, Standby, Cue-Liste) aktualisieren
let presetNames = [];
let standbyPresetName = "";
function fillPresetSelects(list) {
    presetNames = list.map(p => p.name);
    fillDropPresets(list);
    const sb = document.getElementById("standbyPreset");
    sb.innerHTML = "";
    sb.appendChild(new Option("nur dimmen (20 %)", ""));
    list.forEach(p => sb.appendChild(new Option(p.name, p.name)));
    sb.value = standbyPresetName;
    renderCues();
}

function fillDropPresets(list) {
    const sel = document.getElementById("dropPreset");
    sel.innerHTML = "";
    list.forEach(p => {
        const opt = document.createElement("option");
        opt.value = p.name;
        opt.textContent = p.name;
        sel.appendChild(opt);
    });
    if (dropPresetName) sel.value = dropPresetName;
}

function updateDropUi() {
    const isPreset = document.getElementById("dropAction").value === "3";
    document.getElementById("dropPreset").style.display = isPreset ? "" : "none";
    if (dropPresetName) document.getElementById("dropPreset").value = dropPresetName;
}

function saveDropAction() {
    updateDropUi();
    const action = document.getElementById("dropAction").value;
    const preset = document.getElementById("dropPreset").value || "";
    if (action === "3" && !preset) alert("Zuerst ein Preset speichern, das beim Drop aufgerufen werden soll.");
    dropPresetName = preset;
    fetch(`/api/settings?dropAction=${action}&dropPreset=${encodeURIComponent(preset)}`);
}

// --- Auto-Show ---
function saveAutoShow() {
    const on = document.getElementById("autoShow").checked ? 1 : 0;
    const smart = document.getElementById("autoShowSmart").checked ? 1 : 0;
    const beats = document.getElementById("showBeats").value;
    fetch(`/api/settings?autoShow=${on}&showBeats=${beats}&autoShowSmart=${smart}`);
}

const ENERGY_NAMES = ["egal", "ruhig", "mittel", "wild"];

// --- Übergang / Effekt-Parameter / Lautstärke ---
function fmtFade(ms) {
    ms = parseInt(ms);
    return ms === 0 ? "hart" : (ms / 1000).toLocaleString("de-DE", { maximumFractionDigits: 1 }) + " s";
}

function saveFx() {
    const v = id => document.getElementById(id).value;
    fetch(`/api/settings?fadeMs=${v("fadeMs")}&fxWidth=${v("fxWidth")}&fxIntensity=${v("fxIntensity")}`);
}

function saveLevelDim() {
    const on = document.getElementById("levelDim").checked ? 1 : 0;
    fetch(`/api/settings?levelDim=${on}&levelDimDepth=${document.getElementById("levelDimDepth").value}`);
}

function scheduleOverride() {
    fetch("/api/scheduleOverride?on=1");
}

// --- Virtuelle Bühne: Farben aus den gesendeten DMX-Werten berechnen ---
let stageItems = [];
function buildStage(cfg) {
    const box = document.getElementById("stage");
    box.innerHTML = "";
    stageItems = [];
    (cfg.fixtures || []).forEach((fx, index) => {
        const def = (cfg.fixtureTypes || {})[fx.type];
        if (!def) return;
        const el = document.createElement("div");
        el.className = "stage-fx";
        const lamp = document.createElement("div");
        lamp.className = "stage-lamp";
        const dot = document.createElement("div");
        dot.className = "stage-dot";
        if (def.offsetPan >= 0) lamp.appendChild(dot);
        const name = document.createElement("div");
        name.className = "stage-name";
        name.textContent = fx.name;
        el.append(lamp, name);
        box.appendChild(el);
        el.title = "Tippen: Lampe blinkt 5 s";
        el.onclick = () => fetch("/api/identify?id=" + index);
        stageItems.push({ base: fx.start - 1, def, lamp, dot });
    });
    if (!stageItems.length) box.innerHTML = "<div class='preset-hint'>Noch keine Lampen angelegt (DMX-Config).</div>";
}

function drawStage(data) {
    const val = (base, off) => (off >= 0 && base + off < data.length) ? data[base + off] : -1;
    stageItems.forEach(it => {
        const d = it.def;
        let dim = val(it.base, d.offsetDimmer); dim = dim < 0 ? 1 : dim / 255;
        let r = Math.max(0, val(it.base, d.offsetR)), g = Math.max(0, val(it.base, d.offsetG));
        let b = Math.max(0, val(it.base, d.offsetB)), w = Math.max(0, val(it.base, d.offsetW));
        r = Math.min(255, (r + w) * dim); g = Math.min(255, (g + w) * dim); b = Math.min(255, (b + w) * dim);
        const col = `rgb(${r | 0},${g | 0},${b | 0})`;
        it.lamp.style.background = col;
        it.lamp.style.boxShadow = (r + g + b > 30) ? `0 0 18px ${col}` : "none";
        if (d.offsetPan >= 0) {
            const pan = val(it.base, d.offsetPan), tilt = val(it.base, d.offsetTilt);
            it.dot.style.left = (Math.max(0, pan) / 255 * 100) + "%";
            it.dot.style.top = (Math.max(0, tilt) / 255 * 100) + "%";
        }
    });
}

setInterval(() => {
    if (document.hidden || !document.getElementById("stageCard").open || !stageItems.length) return;
    fetch("/api/dmx", { cache: "no-store" }).then(r => r.arrayBuffer())
        .then(buf => drawStage(new Uint8Array(buf))).catch(() => {});
}, 150);

// --- Spektrum ---
function drawSpectrum(spec) {
    const c = document.getElementById("specCanvas");
    if (c.width !== c.clientWidth) c.width = c.clientWidth;
    const g = c.getContext("2d");
    const w = c.width / spec.length;
    g.clearRect(0, 0, c.width, c.height);
    spec.forEach((v, i) => {
        const h = v / 255 * c.height;
        g.fillStyle = `hsl(${(i / spec.length) * 270}, 90%, 55%)`; // Bass rot … Höhen violett
        g.fillRect(i * w + 1, c.height - h, w - 2, h);
    });
}

// --- Halten-Knöpfe (Nebel): solange gedrückt alle 250 ms "on" senden ---
let fogConfigured = false;
// canStart (optional): wird vor dem Start gefragt; false = nichts senden
function setupHoldButton(id, url, canStart) {
    const btn = document.getElementById(id);
    let timer = null;
    const start = e => {
        e.preventDefault();
        if (timer) return;
        if (canStart && !canStart()) return;
        btn.classList.add("on");
        fetch(url + "?on=1");
        timer = setInterval(() => fetch(url + "?on=1"), 250);
    };
    const stop = () => {
        if (!timer) return;
        clearInterval(timer);
        timer = null;
        btn.classList.remove("on");
        fetch(url + "?on=0");
    };
    btn.addEventListener("pointerdown", start);
    ["pointerup", "pointerleave", "pointercancel"].forEach(ev => btn.addEventListener(ev, stop));
    btn.addEventListener("contextmenu", e => e.preventDefault());
    window.addEventListener("blur", stop);
}

function saveFogOnDrop() {
    fetch("/api/settings?fogOnDrop=" + (document.getElementById("fogOnDrop").checked ? 1 : 0));
}

// --- Mikrofon einmessen ---
let calTimer = null;
function toggleCal() {
    const p = document.getElementById("calPanel");
    p.style.display = p.style.display === "none" ? "" : "none";
    if (p.style.display === "") calPoll();
}

function calShow(st) {
    document.getElementById("calSil").textContent = st.silence ? "✓" : "";
    document.getElementById("calMus").textContent = st.music ? "✓" : "";
    document.getElementById("calFill").style.width = (st.phase ? st.progress : 0) + "%";
    if (st.message) document.getElementById("calMsg").textContent = st.message;
}

function calPoll() {
    fetch("/api/calibrate").then(r => r.json()).then(st => {
        calShow(st);
        clearTimeout(calTimer);
        if (st.phase) calTimer = setTimeout(calPoll, 300);
        else if (document.getElementById("calMsg").textContent.startsWith("Messe")) {
            document.getElementById("calMsg").textContent = "Messung fertig.";
        }
    });
}

function calStep(step) {
    if (step !== "apply") document.getElementById("calMsg").textContent = "Messe … bitte warten";
    fetch("/api/calibrate?step=" + step).then(r => r.json()).then(st => {
        calShow(st);
        if (step === "apply" && st.ok) loadConfig(); // Slider steht jetzt auf 100
        if (st.phase) calPoll();
    });
}

// --- Farbpalette ---
const PALETTE_MAX = 8;
function showPalette(p) {
    document.getElementById("paletteOn").checked = !!p.on;
    document.getElementById("paletteSize").value = String(p.size);
    (p.colors || []).forEach((c, i) => { const el = document.getElementById("pal" + i); if (el) el.value = c; });
    updatePaletteUi();
}

function updatePaletteUi() {
    const n = parseInt(document.getElementById("paletteSize").value);
    for (let i = 0; i < PALETTE_MAX; i++) document.getElementById("pal" + i).style.display = i < n ? "" : "none";
}

function savePalette() {
    updatePaletteUi();
    const on = document.getElementById("paletteOn").checked ? 1 : 0;
    const n = document.getElementById("paletteSize").value;
    const cols = [...Array(PALETTE_MAX).keys()].map(i => document.getElementById("pal" + i).value).join(",");
    fetch(`/api/settings?paletteOn=${on}&paletteSize=${n}&palette=${encodeURIComponent(cols)}`);
}

// --- Gruppen ---
function showGroups(groups, fixtures) {
    const rows = document.getElementById("groupRows");
    rows.innerHTML = "";
    let any = false;
    groups.forEach((g, i) => {
        const nr = i + 1;
        const count = fixtures.filter(f => f.group === nr).length;
        if (count === 0) return;
        any = true;
        const send = params => fetch(`/api/setGroup?g=${nr}&` + params);

        const row = document.createElement("div");
        row.className = "group-row";

        const name = document.createElement("input");
        name.type = "text"; name.maxLength = 20;
        name.placeholder = "Gruppe " + nr;
        name.value = g.name || "";
        name.onchange = () => send("name=" + encodeURIComponent(name.value));

        const info = document.createElement("span");
        info.className = "preset-hint";
        info.textContent = count + (count === 1 ? " Lampe" : " Lampen");

        const customLbl = document.createElement("label");
        const custom = document.createElement("input");
        custom.type = "checkbox"; custom.checked = !!g.custom;
        customLbl.append(custom, " eigener Effekt");

        const eff = document.createElement("select");
        ["none"].concat(effects).forEach(e => eff.appendChild(new Option(e === "none" ? "aus" : effectLabel(e), e)));
        eff.value = g.effect || "static";

        const col = document.createElement("input");
        col.type = "color"; col.value = g.color || "#ff0000";

        const update = () => { eff.disabled = col.disabled = !custom.checked; };
        custom.onchange = () => { update(); send("custom=" + (custom.checked ? 1 : 0)); };
        eff.onchange = () => send("effect=" + encodeURIComponent(eff.value));
        col.onchange = () => send("color=" + encodeURIComponent(col.value));
        update();

        row.append(name, info, customLbl, eff, col);
        rows.appendChild(row);
    });
    document.getElementById("groupsCard").style.display = any ? "" : "none";
}

// --- Standby ---
function saveStandby() {
    const min = document.getElementById("standbyMin").value;
    standbyPresetName = document.getElementById("standbyPreset").value;
    fetch(`/api/settings?standbyMin=${min}&standbyPreset=${encodeURIComponent(standbyPresetName)}`);
}

// --- Cue-Liste ---
let cues = [];
let cueRunning = false, cueIndex = -1;

function loadCues() {
    fetch("/api/cues").then(r => r.json()).then(data => {
        cues = data.cues || [];
        document.getElementById("cueLoop").checked = !!data.loop;
        renderCues();
        showCueState(data.running, data.index);
    });
}

function renderCues() {
    const box = document.getElementById("cueRows");
    if (!box) return;
    box.innerHTML = "";
    if (cues.length === 0) {
        box.innerHTML = "<div class='preset-hint'>Noch keine Cues. Erst Presets speichern, dann hier mit „+ Cue“ hinzufügen.</div>";
        return;
    }
    cues.forEach((c, i) => {
        const row = document.createElement("div");
        row.className = "cue-row" + (cueRunning && i === cueIndex ? " active" : "");

        const nr = document.createElement("span");
        nr.className = "cue-nr";
        nr.textContent = i + 1;

        const sel = document.createElement("select");
        presetNames.forEach(n => sel.appendChild(new Option(n, n)));
        if (c.preset && !presetNames.includes(c.preset)) sel.appendChild(new Option(c.preset + " (fehlt)", c.preset));
        sel.value = c.preset;
        sel.onchange = () => c.preset = sel.value;

        const dur = document.createElement("input");
        dur.type = "number"; dur.min = 1; dur.max = 9999; dur.value = c.dur;
        dur.onchange = () => c.dur = Math.max(1, parseInt(dur.value) || 1);

        const unit = document.createElement("select");
        unit.appendChild(new Option("Beats", "1"));
        unit.appendChild(new Option("Sek.", "0"));
        unit.value = c.beats ? "1" : "0";
        unit.onchange = () => c.beats = unit.value === "1";

        const del = document.createElement("button");
        del.className = "preset-del";
        del.textContent = "✕";
        del.onclick = () => { cues.splice(i, 1); renderCues(); };

        row.append(nr, sel, dur, unit, del);
        box.appendChild(row);
    });
}

function addCue() {
    if (presetNames.length === 0) return alert("Bitte zuerst Presets speichern.");
    if (cues.length >= 32) return alert("Maximal 32 Cues.");
    cues.push({ preset: presetNames[0], dur: 16, beats: true });
    renderCues();
}

function saveCues() {
    fetch("/api/saveCues", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(cues)
    }).then(r => alert(r.ok ? "Cue-Liste gespeichert." : "Fehler beim Speichern."));
}

function saveCueLoop() {
    fetch("/api/settings?cueLoop=" + (document.getElementById("cueLoop").checked ? 1 : 0));
}

function cueAction(action) {
    if (action === "start" && cues.length === 0) return alert("Die Cue-Liste ist leer.");
    fetch("/api/cue?action=" + action).then(() => {
        if (action === "start") document.getElementById("autoShow").checked = false;
    });
}

function showCueState(running, index) {
    if (running === cueRunning && index === cueIndex) return;
    cueRunning = running; cueIndex = index;
    const txt = running ? `läuft – Cue ${index + 1} von ${cues.length}` : "";
    document.getElementById("cueState").textContent = txt;
    document.getElementById("cueStateHead").textContent = txt;   // auch im zugeklappten Bereich sichtbar
    renderCues();
}

// --- Blackout ---
function toggleBlackout() {
    const on = !document.getElementById("blackoutBtn").classList.contains("on");
    document.getElementById("blackoutBtn").classList.toggle("on", on);
    fetch("/api/blackout?on=" + (on ? 1 : 0));
}

// --- Presets ---
function loadPresets() {
    fetch("/api/presets").then(r => r.json()).then(list => {
        fillPresetSelects(list);
        const box = document.getElementById("presetButtons");
        box.innerHTML = "";
        if (list.length === 0) {
            box.innerHTML = "<div class='preset-hint'>Noch keine Presets gespeichert.</div>";
            return;
        }
        list.forEach(p => {
            const item = document.createElement("div");
            item.className = "preset-item";

            const btn = document.createElement("button");
            btn.className = "preset-btn";
            btn.dataset.id = p.id;
            btn.style.borderLeftColor = p.color;
            btn.textContent = p.name;
            btn.title = `${p.effect}, Speed ${p.speed} ms, Master ${p.master}`;
            btn.onclick = () => loadPreset(p.id);

            const del = document.createElement("button");
            del.className = "preset-del";
            del.textContent = "✕";
            del.title = "Löschen";
            del.onclick = () => deletePreset(p.id, p.name);

            const en = document.createElement("button");
            en.className = "preset-energy e" + (p.energy || 0);
            en.textContent = p.energy ? "⚡" + ["", "1", "2", "3"][p.energy] : "⚡";
            en.title = "Energie: " + ENERGY_NAMES[p.energy || 0] + " (tippen zum Ändern)";
            en.onclick = () => fetch(`/api/presetEnergy?id=${p.id}&e=${((p.energy || 0) + 1) % 4}`).then(loadPresets);

            item.append(btn, en, del);
            box.appendChild(item);
        });
        markActivePreset();
    });
}

function markActivePreset() {
    document.querySelectorAll(".preset-btn").forEach(b =>
        b.classList.toggle("active", parseInt(b.dataset.id) === shownPreset));
}

function loadPreset(id) {
    fetch("/api/loadPreset?id=" + id).then(() => loadConfig());
}

function savePreset() {
    const input = document.getElementById("presetName");
    const name = input.value.trim();
    if (!name) return alert("Bitte einen Namen eingeben.");
    fetch("/api/savePreset?name=" + encodeURIComponent(name)).then(async r => {
        if (!r.ok) return alert(await r.text());
        input.value = "";
        loadPresets();
    });
}

function deletePreset(id, name) {
    if (!confirm(`Preset "${name}" löschen?`)) return;
    fetch("/api/deletePreset?id=" + id).then(() => loadPresets());
}

function previewMaster(v) { document.getElementById("masterVal").textContent = v; }
function saveMaster(v) { fetch("/api/setMaster?value=" + v); }
function previewThreshold(v) { document.getElementById("thresholdVal").textContent = v; }
function saveThreshold(v) { fetch("/api/setThreshold?value=" + v); }
// Strobe-Knopf: solange gedrückt, alle 250 ms "on" senden (ESP stoppt nach 600 ms ohne Signal)
let strobeTimer = null;
function strobeStart(e) {
    e.preventDefault();
    if (strobeTimer) return;
    document.getElementById("strobeBtn").classList.add("on");
    fetch("/api/strobe?on=1");
    strobeTimer = setInterval(() => fetch("/api/strobe?on=1"), 250);
}
function strobeStop() {
    if (!strobeTimer) return;
    clearInterval(strobeTimer);
    strobeTimer = null;
    document.getElementById("strobeBtn").classList.remove("on");
    fetch("/api/strobe?on=0");
}
function setupStrobeButton() {
    const btn = document.getElementById("strobeBtn");
    btn.addEventListener("pointerdown", strobeStart);
    ["pointerup", "pointerleave", "pointercancel"].forEach(ev => btn.addEventListener(ev, strobeStop));
    btn.addEventListener("contextmenu", e => e.preventDefault()); // kein Langdruck-Menü am Handy
    window.addEventListener("blur", strobeStop);
}

function saveDrop(on) { fetch("/api/setDrop?on=" + (on ? 1 : 0)); }
function setTimer() {
    const t = document.getElementById("timerFader").value;
    document.getElementById("timerVal").textContent = t;
    fetch("/api/setTimer?ms=" + t);
}

function buildEffectButtons() {
    const container = document.getElementById("effectButtons");
    container.innerHTML = effects.map(e => 
        `<button id="btn-${e}" class="effect-btn" onclick="setEffect('${e}')">${esc(effectLabel(e))}</button>`
    ).join("");
}

let currentEffect = "";
function setEffect(name, sendRequest = true) {
    currentEffect = name;
    if(sendRequest) fetch("/api/setEffect?name=" + name);
    document.getElementById("activeEffect").textContent = effectLabel(name);
    
    // Alle Buttons zurücksetzen
    document.querySelectorAll(".effect-btn").forEach(btn => btn.classList.remove("active"));
    
    // Nur den einen Button aktivieren
    const activeBtn = document.getElementById("btn-" + name);
    if (activeBtn) activeBtn.classList.add("active");
}


function pickColor(e) {
    const rect = canvas.getBoundingClientRect();
    const x = (e.clientX || (e.touches ? e.touches[0].clientX : 0)) - rect.left;
    const y = (e.clientY || (e.touches ? e.touches[0].clientY : 0)) - rect.top;
    const imgData = ctx.getImageData(x, y, 1, 1).data;
    const hex = "#" + ((1 << 24) + (imgData[0] << 16) + (imgData[1] << 8) + imgData[2]).toString(16).slice(1);
    
    cursor.style.left = x + 'px';
    cursor.style.top = y + 'px';
    cursor.style.opacity = "1";
    sendQuickColor(hex);
}

function sendQuickColor(hex) {
    fetch("/api/setColor?hex=" + encodeURIComponent(hex));
    setEffect('static');
}

canvas.addEventListener('mousedown', (e) => { isDragging = true; pickColor(e); });
window.addEventListener('mouseup', () => isDragging = false);
canvas.addEventListener('mousemove', (e) => { if(isDragging) pickColor(e); });
canvas.addEventListener('touchstart', (e) => { isDragging = true; pickColor(e); }, {passive:false});
canvas.addEventListener('touchmove', (e) => { if(isDragging) pickColor(e); e.preventDefault(); }, {passive:false});
canvas.addEventListener('touchend', () => isDragging = false);
