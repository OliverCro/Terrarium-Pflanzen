let ws = null;
let reconnectTimer = null;
let reconnectDelay = 500;

let chat = document.getElementById("chat");
let clientId = localStorage.getItem("clientId");
let toggleBtn = document.getElementById("themeToggle");

function generateId() {
    return 'xxxxxxx'.replace(/[x]/g, () =>
    (Math.random() * 16 | 0).toString(16)
    ) + Date.now().toString(16);
}

if (!clientId) {
    clientId = generateId();
    localStorage.setItem("clientId", clientId);
}

document.getElementById("msg").addEventListener("keydown", function (e) {
    if (e.key === "Enter") {
        send();
    }
});

// load saved theme
let darkMode = localStorage.getItem("darkMode") === "true";

if (darkMode) {
    document.body.classList.add("dark");
    toggleBtn.textContent = "☀️";
}

// toggle click
toggleBtn.onclick = () => {
    document.body.classList.toggle("dark");

    let isDark = document.body.classList.contains("dark");
    localStorage.setItem("darkMode", isDark);

    toggleBtn.textContent = isDark ? "☀️" : "🌙";
};

function renderMessage(m) {
    let div = document.createElement("div");
    let isMe = (m.id === clientId);

    div.className = "msg " + (isMe ? "me" : "other");

    // Build the nodes with textContent so user input is never parsed as HTML.
    let user = document.createElement("b");
    user.textContent = m.user || "";
    div.appendChild(user);
    div.appendChild(document.createElement("br"));
    div.appendChild(document.createTextNode(m.text || ""));

    chat.appendChild(div);
}

function handleMessage(event) {
    let data;

    try {
        data = JSON.parse(event.data);
    } catch (e) {
        return; // ignore malformed frames instead of breaking the handler
    }

    // ARRAY = history
    if (Array.isArray(data)) {
        chat.innerHTML = "";
        data.forEach(m => renderMessage(m));
    } else {
        // OBJECT = new message
        renderMessage(data);
    }

    chat.scrollTop = chat.scrollHeight;
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
    };

    ws.onmessage = handleMessage;

    ws.onclose = () => {
        scheduleReconnect();
    };

    ws.onerror = () => {
        if (ws) ws.close();
    };
}

connect();

function send() {
    let input = document.getElementById("msg");
    let text = input.value.trim();

    if (text.length === 0) return;

    // Never call send() on a closed/connecting socket: it throws and would
    // abort this function before the input is cleared.
    if (!ws || ws.readyState !== WebSocket.OPEN) return;

    ws.send(JSON.stringify({
        id: clientId,
        text: text
    }));

    input.value = "";
    input.focus();
}
