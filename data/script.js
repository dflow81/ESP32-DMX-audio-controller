// ---------------------------------------------------------
// GLOBALS
// ---------------------------------------------------------

let presets = [];
let previewTimeout = null;

// ---------------------------------------------------------
// LOAD PRESETS
// ---------------------------------------------------------

async function loadPresets() {
    let res = await fetch("/api/presets");
    let data = await res.json();
    presets = data.presets;

    let grid = document.getElementById("presetGrid");
    if (!grid) return;

    grid.innerHTML = "";

    presets.forEach(p => {
        let card = document.createElement("div");
        card.className = "presetCard";

        card.innerHTML = `
            <div class="presetIcon">${p.icon}</div>
            <div class="presetName">${p.name}</div>
        `;

        // Load on click
        card.onclick = () => loadPreset(p.id);

        // Preview on hover
        card.onmouseenter = () => previewPreset(p.id);
        card.onmouseleave = () => cancelPreview();

        grid.appendChild(card);
    });
}

// ---------------------------------------------------------
// PREVIEW FADE
// ---------------------------------------------------------

function previewPreset(id) {
    if (previewTimeout) clearTimeout(previewTimeout);

    previewTimeout = setTimeout(() => {
        fetch(`/api/preset/preview?id=${id}`);
    }, 150);
}

function cancelPreview() {
    // No cancel API needed — fade ends automatically
}

// ---------------------------------------------------------
// LOAD PRESET
// ---------------------------------------------------------

async function loadPreset(id) {
    await fetch(`/api/preset/load?id=${id}`);
}

// ---------------------------------------------------------
// SAVE PRESET
// ---------------------------------------------------------

async function savePreset() {
    let name = prompt("Name des Presets:");
    if (!name) return;

    let icon = prompt("Icon (Emoji):", "🎛️");
    if (!icon) icon = "🎛️";

    await fetch("/api/preset/save", {
        method: "POST",
        body: JSON.stringify({ name, icon })
    });

    loadPresets();
}

// ---------------------------------------------------------
// STATUS POLLING (LEVEL, BEAT, BPM)
// ---------------------------------------------------------

async function pollStatus() {
    let res = await fetch("/api/status");
    let data = await res.json();

    // Level bar
    let bar = document.getElementById("levelBar");
    if (bar) {
        let pct = Math.min(100, data.level * 300);
        bar.style.setProperty("--level", pct + "%");
        bar.style.width = pct + "%";
        bar.style.background = pct > 60 ? "#0f0" : "#090";
    }

    // Beat dot
    let dot = document.getElementById("beatDot");
    if (dot) {
        if (data.beat) dot.classList.add("active");
        else dot.classList.remove("active");
    }

    // BPM
    let bpm = document.getElementById("bpmText");
    if (bpm) {
        bpm.textContent = "BPM: " + (data.bpm > 1 ? data.bpm.toFixed(1) : "--");
    }
}

setInterval(pollStatus, 80);

// ---------------------------------------------------------
// CONFIG PAGE
// ---------------------------------------------------------

async function loadConfig() {
    let res = await fetch("/api/config");
    let cfg = await res.json();

    if (document.getElementById("sta_ssid"))
        document.getElementById("sta_ssid").value = cfg.sta_ssid;

    if (document.getElementById("sta_pass"))
        document.getElementById("sta_pass").value = cfg.sta_pass;

    if (document.getElementById("beatFadeEnabled"))
        document.getElementById("beatFadeEnabled").checked = cfg.beatFadeEnabled;

    if (document.getElementById("beatFadeKick"))
        document.getElementById("beatFadeKick").checked = cfg.beatFadeKick;

    if (document.getElementById("beatFadeSnare"))
        document.getElementById("beatFadeSnare").checked = cfg.beatFadeSnare;

    if (document.getElementById("beatFadeHiHat"))
        document.getElementById("beatFadeHiHat").checked = cfg.beatFadeHiHat;

    if (document.getElementById("beatFadeStrength"))
        document.getElementById("beatFadeStrength").value = cfg.beatFadeStrength;

    if (document.getElementById("beatFadeEvery"))
        document.getElementById("beatFadeEvery").value = cfg.beatFadeEvery;
}

async function saveConfig() {
    let cfg = {
        sta_ssid: document.getElementById("sta_ssid").value,
        sta_pass: document.getElementById("sta_pass").value,
        beatFadeEnabled: document.getElementById("beatFadeEnabled").checked,
        beatFadeKick: document.getElementById("beatFadeKick").checked,
        beatFadeSnare: document.getElementById("beatFadeSnare").checked,
        beatFadeHiHat: document.getElementById("beatFadeHiHat").checked,
        beatFadeStrength: parseInt(document.getElementById("beatFadeStrength").value),
        beatFadeEvery: parseInt(document.getElementById("beatFadeEvery").value)
    };

    await fetch("/api/config", {
        method: "POST",
        body: JSON.stringify(cfg)
    });

    alert("Gespeichert!");
}

// ---------------------------------------------------------
// NAVIGATION
// ---------------------------------------------------------

function openConfig() {
    location.href = "/config";
}

// ---------------------------------------------------------
// INIT
// ---------------------------------------------------------

window.onload = () => {
    if (document.getElementById("presetGrid")) loadPresets();
    if (document.getElementById("sta_ssid")) loadConfig();
};
