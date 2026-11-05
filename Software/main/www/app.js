let ws = null;
let reconnectTimer = null;
let reconnectDelay = 500;

let updateTimer = null;
let pending = {};

const statusEl = document.getElementById("status");
const temperatureEl = document.getElementById("temperature");
const humidityEl = document.getElementById("humidity");

const sliders = {
    heater: document.getElementById("heater"),
    fan: document.getElementById("fan"),
    lamp: document.getElementById("lamp"),
};

const outputs = {
    heater: document.getElementById("heaterValue"),
    fan: document.getElementById("fanValue"),
    lamp: document.getElementById("lampValue"),
    pump: document.getElementById("pumpValue"),
};

const pumpEl = document.getElementById("pump");

/* True while this client is operating a control. Remote updates for that
 * control are ignored until the user lets go, otherwise an echoed state would
 * yank the slider out from under their finger. */
const active = {
    heater: false,
    fan: false,
    lamp: false,
    pump: false,
};

function isControlActive(name) {
    return active[name];
}

function clampLevel(value) {
    let number = Number(value);

    if (!Number.isFinite(number)) return 0;

    return Math.max(0, Math.min(100, Math.round(number)));
}

function formatSensor(value) {
    return Number.isFinite(Number(value)) ? Number(value).toFixed(1) : "--";
}

function setSlider(name, value) {
    let level = clampLevel(value);
    let slider = sliders[name];

    slider.value = level;
    slider.style.setProperty("--fill", level + "%");
    outputs[name].textContent = level + " %";
}

function setPump(value) {
    let on = Number(value) >= 1;

    pumpEl.checked = on;
    outputs.pump.textContent = on ? "An" : "Aus";
}

function applyState(state) {
    if (!state || typeof state !== "object" || Array.isArray(state)) return;

    if (typeof state.heater === "number" && !isControlActive("heater")) {
        setSlider("heater", state.heater);
    }
    if (typeof state.fan === "number" && !isControlActive("fan")) {
        setSlider("fan", state.fan);
    }
    if (typeof state.lamp === "number" && !isControlActive("lamp")) {
        setSlider("lamp", state.lamp);
    }
    if (typeof state.pump === "number" && !isControlActive("pump")) {
        setPump(state.pump);
    }

    if (typeof state.temperature === "number") {
        temperatureEl.textContent = formatSensor(state.temperature);
    }
    if (typeof state.humidity === "number") {
        humidityEl.textContent = formatSensor(state.humidity);
    }
}

function setStatus(connected) {
    statusEl.textContent = connected ? "verbunden" : "getrennt";
    statusEl.classList.toggle("online", connected);
    statusEl.classList.toggle("offline", !connected);
}

function scheduleReconnect() {
    if (reconnectTimer !== null) return;

    reconnectTimer = setTimeout(() => {
        reconnectTimer = null;
        connect();
    }, reconnectDelay);

    reconnectDelay = Math.min(reconnectDelay * 2, 5000);
}

function connect() {
    let scheme = location.protocol === "https:" ? "wss" : "ws";
    ws = new WebSocket(`${scheme}://${location.host}/ws`);

    ws.onopen = () => {
        reconnectDelay = 500;
        setStatus(true);
    };

    ws.onmessage = (event) => {
        let data;

        try {
            data = JSON.parse(event.data);
        } catch (e) {
            return;
        }

        applyState(data);
    };

    ws.onclose = () => {
        setStatus(false);
        scheduleReconnect();
    };

    ws.onerror = () => {
        if (ws) ws.close();
    };
}

// Changes are batched briefly so dragging a slider does not flood the ESP32.
function queuePatch(key, value) {
    pending[key] = value;

    if (updateTimer !== null) return;
    updateTimer = setTimeout(flushPatch, 100);
}

function flushPatch() {
    updateTimer = null;

    let patch = pending;
    pending = {};

    if (!ws || ws.readyState !== WebSocket.OPEN) return;

    ws.send(JSON.stringify(patch));
}

Object.keys(sliders).forEach((name) => {
    sliders[name].addEventListener("input", () => {
        let value = clampLevel(sliders[name].value);

        setSlider(name, value);
        queuePatch(name, value);
    });

    sliders[name].addEventListener("pointerdown", () => {
        active[name] = true;
    });
});

pumpEl.addEventListener("change", () => {
    let value = pumpEl.checked ? 1 : 0;

    setPump(value);
    queuePatch("pump", value);
});

pumpEl.addEventListener("pointerdown", () => {
    active.pump = true;
});

// Release anywhere (including outside the control) ends the local override.
["pointerup", "pointercancel"].forEach((eventName) => {
    window.addEventListener(eventName, () => {
        Object.keys(active).forEach((name) => {
            active[name] = false;
        });
    });
});

Object.keys(sliders).forEach((name) => setSlider(name, sliders[name].value));
setPump(0);

connect();
