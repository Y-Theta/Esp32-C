let deviceConfig = {
    ssid: "--",
    freq: 470000000,
    power: 22,
    sf: 7,
    bw: 125,
    cr: 1,
    sync: "0x12",
    mode: "tx"
};

let rxTimer = null;

document.addEventListener("DOMContentLoaded", () => {
    bindTabs();
    bindConfigForm();
    bindMessageControls();

    loadConfig();
});

function bindTabs() {
    const tabButtons = document.querySelectorAll(".tab-btn");

    tabButtons.forEach(btn => {
        btn.addEventListener("click", () => {
            const tabId = btn.dataset.tab;

            document.querySelectorAll(".tab-btn").forEach(b => {
                b.classList.remove("active");
            });

            document.querySelectorAll(".tab-page").forEach(page => {
                page.classList.remove("active");
            });

            btn.classList.add("active");
            document.getElementById(tabId).classList.add("active");

            if (tabId === "messageTab") {
                updateMessagePanel();
            }
        });
    });
}

function bindConfigForm() {
    const form = document.getElementById("configForm");

    form.addEventListener("submit", async event => {
        event.preventDefault();

        const config = {
            freq: Number(document.getElementById("freq").value),
            power: Number(document.getElementById("power").value),
            sf: Number(document.getElementById("sf").value),
            bw: Number(document.getElementById("bw").value),
            cr: Number(document.getElementById("cr").value),
            sync: document.getElementById("sync").value.trim(),
            mode: document.getElementById("mode").value
        };

        try {
            const response = await fetch("/api/config", {
                method: "POST",
                headers: {
                    "Content-Type": "application/json"
                },
                body: JSON.stringify(config)
            });

            if (!response.ok) {
                throw new Error("保存失败");
            }

            const result = await response.json();

            if (result.ok) {
                deviceConfig = {
                    ...deviceConfig,
                    ...config
                };

                showToast("配置已保存并应用");
                updateMessagePanel();
            } else {
                showToast(result.message || "保存失败");
            }
        } catch (err) {
            showToast("请求失败：" + err.message);
        }
    });
}

function bindMessageControls() {
    const txMessage = document.getElementById("txMessage");
    const charCount = document.getElementById("charCount");
    const sendBtn = document.getElementById("sendBtn");
    const clearConsoleBtn = document.getElementById("clearConsoleBtn");

    txMessage.addEventListener("input", () => {
        let value = txMessage.value;

        /**
         * 限制为英文字符。
         * 这里使用 ASCII 范围：
         * 允许英文、数字、常用符号、空格、换行等。
         */
        value = value.replace(/[^\x00-\x7F]/g, "");

        if (value.length > 100) {
            value = value.substring(0, 100);
        }

        txMessage.value = value;
        charCount.textContent = value.length;
    });

    sendBtn.addEventListener("click", sendMessage);

    clearConsoleBtn.addEventListener("click", () => {
        document.getElementById("rxConsole").textContent = "";
    });
}

async function loadConfig() {
    try {
        const response = await fetch("/api/config");

        if (!response.ok) {
            throw new Error("无法获取设备参数");
        }

        const config = await response.json();

        deviceConfig = {
            ...deviceConfig,
            ...config
        };

        fillConfigForm(deviceConfig);
        updateMessagePanel();
    } catch (err) {
        showToast("加载配置失败：" + err.message);
    }
}

function fillConfigForm(config) {
    document.getElementById("ssid").textContent = config.ssid || "--";
    document.getElementById("freq").value = config.freq;
    document.getElementById("power").value = config.power;
    document.getElementById("sf").value = config.sf;
    document.getElementById("bw").value = config.bw;
    document.getElementById("cr").value = config.cr;
    document.getElementById("sync").value = config.sync;
    document.getElementById("mode").value = config.mode;
}

function updateMessagePanel() {
    const mode = document.getElementById("mode").value || deviceConfig.mode;

    deviceConfig.mode = mode;

    const currentMode = document.getElementById("currentMode");
    const txPanel = document.getElementById("txPanel");
    const rxPanel = document.getElementById("rxPanel");

    if (mode === "tx") {
        currentMode.textContent = "发送模式";

        txPanel.classList.remove("hidden");
        rxPanel.classList.add("hidden");

        stopRxPolling();
    } else {
        currentMode.textContent = "接收模式";

        txPanel.classList.add("hidden");
        rxPanel.classList.remove("hidden");

        startRxPolling();
    }
}

async function sendMessage() {
    const txMessage = document.getElementById("txMessage");
    const message = txMessage.value;

    if (!message) {
        showToast("请输入要发送的消息");
        return;
    }

    if (message.length > 100) {
        showToast("消息不能超过 100 个英文字符");
        return;
    }

    if (/[^\x00-\x7F]/.test(message)) {
        showToast("只能发送英文字符");
        return;
    }

    try {
        const response = await fetch("/api/send", {
            method: "POST",
            headers: {
                "Content-Type": "application/json"
            },
            body: JSON.stringify({
                message: message
            })
        });

        if (!response.ok) {
            throw new Error("发送失败");
        }

        const result = await response.json();

        if (result.ok) {
            showToast("消息已发送");
            txMessage.value = "";
            document.getElementById("charCount").textContent = "0";
        } else {
            showToast(result.message || "发送失败");
        }
    } catch (err) {
        showToast("请求失败：" + err.message);
    }
}

function startRxPolling() {
    stopRxPolling();

    fetchRxMessages();

    rxTimer = setInterval(fetchRxMessages, 1000);
}

function stopRxPolling() {
    if (rxTimer) {
        clearInterval(rxTimer);
        rxTimer = null;
    }
}

async function fetchRxMessages() {
    try {
        const response = await fetch("/api/receive");

        if (response.status === 204) {
            return;
        }

        if (!response.ok) {
            return;
        }

        const text = await response.text();

        if (!text) {
            return;
        }

        const result = JSON.parse(text);

        if (!result.ok) {
            return;
        }

        if (!Array.isArray(result.messages)) {
            return;
        }

        if (result.messages.length === 0) {
            return;
        }

        appendRxMessages(result.messages);

    } catch (err) {
        return;
    }
}

function appendRxMessages(messages) {
    messages.forEach(msg => {
        if (typeof msg === "string") {
            appendConsoleLine(msg);
        } else if (msg.message) {
            const rssi = msg.rssi !== undefined ? ` RSSI:${msg.rssi}` : "";
            const snr = msg.snr !== undefined ? ` SNR:${msg.snr}` : "";
            appendConsoleLine(`[RX]${rssi}${snr} ${msg.message}`);
        }
    });
}

function appendConsoleLine(text) {
    const consoleEl = document.getElementById("rxConsole");

    const time = new Date().toLocaleTimeString();
    consoleEl.textContent += `[${time}] ${text}\n`;

    consoleEl.scrollTop = consoleEl.scrollHeight;
}

function showToast(message) {
    const toast = document.getElementById("toast");

    toast.textContent = message;
    toast.classList.remove("hidden");

    setTimeout(() => {
        toast.classList.add("hidden");
    }, 2500);
}