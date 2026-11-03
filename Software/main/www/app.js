let ws = new WebSocket(`ws://${location.host}/ws`);
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
    div.innerHTML = `<b>${m.user}</b><br>${m.text}`;

    chat.appendChild(div);
}

ws.onmessage = (event) => {
    let data = JSON.parse(event.data);

    // ARRAY = history
    if (Array.isArray(data)) {
        chat.innerHTML = "";
        data.forEach(m => {
            renderMessage(m);
        });
        chat.scrollTop = chat.scrollHeight;
        return;
    }

    // OBJECT = new message
    renderMessage(data);
    chat.scrollTop = chat.scrollHeight;
};

function send() {
    let input = document.getElementById("msg");
    let text = input.value.trim();

    if (text.length === 0) return;

    ws.send(JSON.stringify({
        id: clientId,
        text: text
    }));

    input.value = "";
    input.focus();
}
