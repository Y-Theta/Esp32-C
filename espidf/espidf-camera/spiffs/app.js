// 全局配置
let currentConfig = {
    wifiSsid: '',
    wifiPass: '',
    postServer: '',
    postPort: 8080,
    postInterval: 60,
    jpegQuantity: 1,
    frameSize: 10,
    wbMode: 0,
    contrast: 3,
    saturation: 3,
    brightness: 4,
    specialEffect: 0,
    startPoster: 'off',
    waitApFirst: 'off',
    nickname: '5mpCamera',
    timeZone: 'GMT+0'
};

let cameraStatus = {
    initialized: false,
    frameSize: 10,
    jpegQuantity: 1,
    wbMode: 0,
    contrast: 3,
    saturation: 3,
    brightness: 4,
    specialEffect: 0
};

let isTakingPhoto = false;

// 初始化
document.addEventListener('DOMContentLoaded', () => {
    initTabs();
    initRangeInputs();
    initButtons();
    loadConfig();
    loadCameraStatus();
});

// Tab 导航
function initTabs() {
    const tabs = document.querySelectorAll('.tab');
    const tabPanes = document.querySelectorAll('.tab-pane');

    tabs.forEach(tab => {
        tab.addEventListener('click', () => {
            tabs.forEach(t => t.classList.remove('active'));
            tabPanes.forEach(p => p.classList.remove('active'));

            tab.classList.add('active');
            const targetId = tab.getAttribute('data-tab') + '-tab';
            document.getElementById(targetId).classList.add('active');
        });
    });
}

// Range 滑块实时显示数值
function initRangeInputs() {
    const rangeMap = {
        'contrast': 'contrast-value',
        'saturation': 'saturation-value',
        'brightness': 'brightness-value'
    };

    Object.entries(rangeMap).forEach(([inputId, valueId]) => {
        const input = document.getElementById(inputId);
        const valueSpan = document.getElementById(valueId);
        if (input && valueSpan) {
            input.addEventListener('input', () => {
                valueSpan.textContent = input.value;
            });
        }
    });
}

// 按钮事件
function initButtons() {
    document.getElementById('capture-btn').addEventListener('click', capturePhoto);
    document.getElementById('save-config-btn').addEventListener('click', saveConfig);
    document.getElementById('connect-sta-btn').addEventListener('click', connectToSTA);
    document.getElementById('apply-camera-btn').addEventListener('click', applyCameraSettings);
}

// 加载系统配置
async function loadConfig() {
    try {
        const response = await fetch('/api/config');
        if (response.ok) {
            currentConfig = await response.json();
            fillSystemForm(currentConfig);
            fillCameraForm(currentConfig);
            console.log('Config loaded:', currentConfig);
        }
    } catch (e) {
        console.error('Failed to load config:', e);
    }
}

// 加载相机状态
async function loadCameraStatus() {
    try {
        const response = await fetch('/api/camera/status');
        if (response.ok) {
            cameraStatus = await response.json();
            updateCameraInitStatus(cameraStatus.initialized);
            console.log('Camera status:', cameraStatus);
        }
    } catch (e) {
        console.error('Failed to load camera status:', e);
    }
}

// 更新相机初始化状态显示
function updateCameraInitStatus(initialized) {
    const statusEl = document.getElementById('camera-init-status');
    const dot = statusEl.querySelector('.status-dot');
    const text = statusEl.querySelector('span:last-child');

    if (initialized) {
        dot.classList.remove('offline');
        text.textContent = '已初始化';
    } else {
        dot.classList.add('offline');
        text.textContent = '未初始化';
    }
}

// 填充系统设置表单
function fillSystemForm(config) {
    document.getElementById('wifi-ssid').value = config.wifiSsid || '';
    document.getElementById('wifi-password').value = config.wifiPass || '';
    document.getElementById('server-address').value = config.postServer || '';
    document.getElementById('server-port').value = config.postPort || 8080;
    document.getElementById('upload-interval').value = config.postInterval || 60;
    document.getElementById('nickname').value = config.nickname || '5mpCamera';
    document.getElementById('timezone').value = config.timeZone || 'GMT+0';
}

// 填充相机设置表单
function fillCameraForm(config) {
    setSelectValue('frame-size', config.frameSize);
    setSelectValue('jpeg-quality', config.jpegQuantity);
    setSelectValue('wb-mode', config.wbMode);
    setSelectValue('special-effect', config.specialEffect);

    document.getElementById('contrast').value = config.contrast;
    document.getElementById('contrast-value').textContent = config.contrast;
    document.getElementById('saturation').value = config.saturation;
    document.getElementById('saturation-value').textContent = config.saturation;
    document.getElementById('brightness').value = config.brightness;
    document.getElementById('brightness-value').textContent = config.brightness;
}

function setSelectValue(id, value) {
    const select = document.getElementById(id);
    if (!select) return;
    for (let i = 0; i < select.options.length; i++) {
        if (parseInt(select.options[i].value) === value) {
            select.selectedIndex = i;
            break;
        }
    }
}

// 拍照功能
async function capturePhoto() {
    if (isTakingPhoto) return;
    isTakingPhoto = true;

    const statusElement = document.getElementById('capture-status');
    const imageContainer = document.getElementById('captured-image');
    const placeholder = document.getElementById('photo-placeholder');
    const photoInfo = document.getElementById('photo-info');
    const captureBtn = document.getElementById('capture-btn');

    captureBtn.disabled = true;
    statusElement.textContent = '正在拍照...';
    document.querySelector('#capture-status').parentElement.querySelector('.status-dot').classList.add('busy');

    try {
        const response = await fetch('/api/take-photo', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' }
        });

        if (response.ok) {
            const data = await response.json();

            if (data.success) {
                statusElement.textContent = '拍照成功';
                document.getElementById('photo-size').textContent = formatBytes(data.photoSize);
                document.getElementById('photo-time').textContent = new Date().toLocaleString();

                placeholder.style.display = 'none';
                imageContainer.style.display = 'block';
                photoInfo.style.display = 'flex';

                await new Promise(resolve => setTimeout(resolve, 300));

                const img = document.getElementById('captured-photo');
                img.src = data.photoUrl + '?t=' + Date.now();
                img.onload = () => {
                    imageContainer.style.display = 'block';
                };
                img.onerror = () => {
                    statusElement.textContent = '图片加载失败';
                };

                // 拍照会初始化相机，刷新状态
                loadCameraStatus();
            } else {
                statusElement.textContent = '拍照失败: ' + (data.error || '未知错误');
            }
        } else {
            statusElement.textContent = '请求失败';
        }
    } catch (e) {
        statusElement.textContent = '错误: ' + e.toString();
        console.error('Capture failed:', e);
    } finally {
        isTakingPhoto = false;
        captureBtn.disabled = false;
        document.querySelector('#capture-status').parentElement.querySelector('.status-dot').classList.remove('busy');
    }
}

// 应用相机设置（热重载，不重启）
async function applyCameraSettings() {
    const btn = document.getElementById('apply-camera-btn');
    const statusEl = document.getElementById('camera-settings-status');

    btn.disabled = true;
    showMessage(statusEl, '正在应用...', 'info');

    const settings = {
        frameSize: parseInt(document.getElementById('frame-size').value),
        quality: parseInt(document.getElementById('jpeg-quality').value),
        wbMode: parseInt(document.getElementById('wb-mode').value),
        specialEffect: parseInt(document.getElementById('special-effect').value),
        contrast: parseInt(document.getElementById('contrast').value),
        saturation: parseInt(document.getElementById('saturation').value),
        brightness: parseInt(document.getElementById('brightness').value)
    };

    try {
        // 发送所有设置请求（并行）
        const requests = [
            fetch('/api/camera/frame-size', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ frameSize: settings.frameSize })
            }),
            fetch('/api/camera/quality', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ quality: settings.quality })
            }),
            fetch('/api/camera/white-balance', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ wbMode: settings.wbMode })
            }),
            fetch('/api/camera/special-effect', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ effect: settings.specialEffect })
            }),
            fetch('/api/camera/contrast', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ contrast: settings.contrast })
            }),
            fetch('/api/camera/saturation', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ saturation: settings.saturation })
            }),
            fetch('/api/camera/brightness', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ brightness: settings.brightness })
            })
        ];

        const results = await Promise.all(requests);
        const allOk = results.every(r => r.ok);

        if (allOk) {
            showMessage(statusEl, '相机设置已应用', 'success');
            await loadCameraStatus();
        } else {
            showMessage(statusEl, '部分设置应用失败', 'error');
        }
    } catch (e) {
        showMessage(statusEl, '错误: ' + e.toString(), 'error');
        console.error('Apply camera settings failed:', e);
    } finally {
        btn.disabled = false;
    }
}

// 保存系统配置（需要重启）
async function saveConfig() {
    const saveBtn = document.getElementById('save-config-btn');
    const statusElement = document.getElementById('save-status');

    saveBtn.disabled = true;
    showMessage(statusElement, '正在保存...', 'info');

    currentConfig.wifiSsid = document.getElementById('wifi-ssid').value;
    currentConfig.wifiPass = document.getElementById('wifi-password').value;
    currentConfig.postServer = document.getElementById('server-address').value;
    currentConfig.postPort = parseInt(document.getElementById('server-port').value);
    currentConfig.postInterval = parseInt(document.getElementById('upload-interval').value);
    currentConfig.nickname = document.getElementById('nickname').value;
    currentConfig.timeZone = document.getElementById('timezone').value;

    // 同时保存相机参数到配置文件中
    currentConfig.jpegQuantity = parseInt(document.getElementById('jpeg-quality').value);
    currentConfig.frameSize = parseInt(document.getElementById('frame-size').value);
    currentConfig.wbMode = parseInt(document.getElementById('wb-mode').value);
    currentConfig.specialEffect = parseInt(document.getElementById('special-effect').value);
    currentConfig.contrast = parseInt(document.getElementById('contrast').value);
    currentConfig.saturation = parseInt(document.getElementById('saturation').value);
    currentConfig.brightness = parseInt(document.getElementById('brightness').value);

    try {
        const response = await fetch('/api/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(currentConfig)
        });

        if (response.ok) {
            const data = await response.json();
            if (data.success) {
                showMessage(statusElement, '保存成功！正在重启...', 'success');
            }
        } else {
            showMessage(statusElement, '保存失败', 'error');
        }
    } catch (e) {
        showMessage(statusElement, '错误: ' + e.toString(), 'error');
        console.error('Save failed:', e);
    } finally {
        saveBtn.disabled = false;
    }
}

// 连接到 STA 模式
async function connectToSTA() {
    const connectBtn = document.getElementById('connect-sta-btn');
    connectBtn.disabled = true;

    try {
        const response = await fetch('/api/connect-sta', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' }
        });

        if (response.ok) {
            const data = await response.json();
            console.log('Connecting to STA:', data);
        }
    } catch (e) {
        console.error('Connect failed:', e);
    } finally {
        connectBtn.disabled = false;
    }
}

// 工具函数：显示消息
function showMessage(element, text, type) {
    element.textContent = text;
    element.className = 'message ' + type;
    element.style.display = 'block';
}

// 工具函数：格式化字节
function formatBytes(bytes) {
    if (bytes === 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
}