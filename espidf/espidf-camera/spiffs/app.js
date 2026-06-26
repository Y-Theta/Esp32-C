// 全局配置
let currentConfig = {
    wifiList: [{ssid:'', pass:''}, {ssid:'', pass:''}, {ssid:'', pass:''}],
    postServer: '',
    postUsePut: false,
    jpegQuantity: 1,
    frameSize: 10,
    streamFps: 25,
    wbMode: 0,
    contrast: 3,
    saturation: 3,
    brightness: 4,
    specialEffect: 0
};

let cameraStatus = {
    initialized: false,
    frameSize: 10,
    jpegQuantity: 1,
    streamFps: 25,
    wbMode: 0,
    contrast: 3,
    saturation: 3,
    brightness: 4,
    specialEffect: 0
};

let isTakingPhoto = false;

// 内存状态刷新定时器
let memoryRefreshTimer = null;

// ===== 监控流相关 (WebSocket 推流) =====
let monitorActive = false;
let streamWs = null;
let frameCount = 0;
let streamTotalBytes = 0;
let fpsCounter = { frames: 0, lastTs: 0, fps: 0 };
let currentBlobUrl = null;
let currentFrameIntervalMs = 50;
let lastRenderTs = 0;

const MAX_WIFI_SSIDS = 3;

// 更新内存状态显示
async function updateMemoryStatus() {
    try {
        const response = await fetch('/api/memory-status');
        if (response.ok) {
            const data = await response.json();
            
            document.getElementById('ram-used').textContent = formatBytes(data.ram.used);
            document.getElementById('ram-total').textContent = formatBytes(data.ram.total);
            
            document.getElementById('psram-used').textContent = formatBytes(data.psram.used);
            document.getElementById('psram-total').textContent = formatBytes(data.psram.total);
        }
    } catch (e) {
        console.error('Failed to update memory status:', e);
    }
}

// 生成WiFi列表表单
function renderWifiList() {
    const container = document.getElementById('wifi-list');
    container.innerHTML = '';
    
    for (let i = 0; i < MAX_WIFI_SSIDS; i++) {
        const wifiItem = document.createElement('div');
        wifiItem.className = 'wifi-item';
        wifiItem.innerHTML = `
            <div class="wifi-item-header">
                <span class="wifi-item-number">热点 ${i + 1}</span>
            </div>
            <div class="wifi-item-fields">
                <div class="form-group">
                    <label for="wifi-ssid-${i}">热点名称 (SSID)</label>
                    <input type="text" id="wifi-ssid-${i}" placeholder="输入 WiFi 名称" autocomplete="off">
                </div>
                <div class="form-group">
                    <label for="wifi-pass-${i}">热点密码</label>
                    <input type="password" id="wifi-pass-${i}" placeholder="输入 WiFi 密码" autocomplete="off">
                </div>
            </div>
        `;
        container.appendChild(wifiItem);
    }
}

// 初始化
document.addEventListener('DOMContentLoaded', () => {
    renderWifiList();
    initTabs();
    initRangeInputs();
    initButtons();
    loadConfig();
    loadCameraStatus();
    
    updateMemoryStatus();
    
    memoryRefreshTimer = setInterval(updateMemoryStatus, 3000);
});

// Tab 导航
function initTabs() {
    const tabs = document.querySelectorAll('.tab');
    const tabPanes = document.querySelectorAll('.tab-pane');

    tabs.forEach(tab => {
        tab.addEventListener('click', () => {
            const targetTab = tab.getAttribute('data-tab');
            const currentActiveTab = document.querySelector('.tab.active').getAttribute('data-tab');
            
            tabs.forEach(t => t.classList.remove('active'));
            tabPanes.forEach(p => p.classList.remove('active'));

            tab.classList.add('active');
            const targetId = targetTab + '-tab';
            document.getElementById(targetId).classList.add('active');

            if (currentActiveTab === 'monitor' && targetTab !== 'monitor') {
                stopMonitor();
            }
        });
    });
}

// Range 滑块实时显示数值
function initRangeInputs() {
    const rangeMap = {
        'contrast': 'contrast-value',
        'saturation': 'saturation-value',
        'brightness': 'brightness-value',
        'stream-fps': 'stream-fps-value'
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
    document.getElementById('apply-camera-btn').addEventListener('click', applyCameraSettings);
    document.getElementById('monitor-start-btn').addEventListener('click', startMonitor);
    document.getElementById('monitor-stop-btn').addEventListener('click', stopMonitor);
}

// 加载系统配置
async function loadConfig() {
    try {
        const response = await fetch('/api/config');
        if (response.ok) {
            currentConfig = await response.json();
            
            // 处理WiFi列表 - 兼容旧版本单WiFi配置
            if (!currentConfig.wifiList || !Array.isArray(currentConfig.wifiList)) {
                currentConfig.wifiList = [];
                for (let i = 0; i < MAX_WIFI_SSIDS; i++) {
                    if (i === 0) {
                        currentConfig.wifiList.push({
                            ssid: currentConfig.wifiSsid || '',
                            pass: currentConfig.wifiPass || ''
                        });
                    } else {
                        currentConfig.wifiList.push({ssid: '', pass: ''});
                    }
                }
            }
            
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
    // 填充WiFi列表
    for (let i = 0; i < MAX_WIFI_SSIDS; i++) {
        const ssidInput = document.getElementById(`wifi-ssid-${i}`);
        const passInput = document.getElementById(`wifi-pass-${i}`);
        if (ssidInput && config.wifiList[i]) {
            ssidInput.value = config.wifiList[i].ssid || '';
        }
        if (passInput && config.wifiList[i]) {
            passInput.value = config.wifiList[i].pass || '';
        }
    }
    
    document.getElementById('post-server').value = config.postServer || '';
    document.getElementById('post-use-put').checked = config.postUsePut || false;
}

// 填充相机设置表单
function fillCameraForm(config) {
    setSelectValue('frame-size', config.frameSize);
    setSelectValue('jpeg-quality', config.jpegQuantity);
    setSelectValue('wb-mode', config.wbMode);
    setSelectValue('special-effect', config.specialEffect);
    setSelectValue('stream-frame-size', config.streamFrameSize);

    document.getElementById('contrast').value = config.contrast;
    document.getElementById('contrast-value').textContent = config.contrast;
    document.getElementById('saturation').value = config.saturation;
    document.getElementById('saturation-value').textContent = config.saturation;
    document.getElementById('brightness').value = config.brightness;
    document.getElementById('brightness-value').textContent = config.brightness;

    if (config.streamFps) {
        document.getElementById('stream-fps').value = config.streamFps;
        document.getElementById('stream-fps-value').textContent = config.streamFps;
    }
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

// 查询拍照状态
async function checkPhotoStatus(targetVersion) {
    try {
        const response = await fetch('/api/photo-status');
        if (response.ok) {
            const data = await response.json();
            return data;
        }
    } catch (e) {
        console.error('Failed to check photo status:', e);
    }
    return null;
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

    placeholder.style.display = 'block';
    imageContainer.style.display = 'none';
    const img = document.getElementById('captured-photo');
    if (img.dataset.blobUrl) {
        URL.revokeObjectURL(img.dataset.blobUrl);
        delete img.dataset.blobUrl;
    }
    img.src = 'data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7';

    try {
        const response = await fetch('/api/take-photo', {
            method: 'POST',
            headers: { 
                'Content-Type': 'application/json',
                'Cache-Control': 'no-cache, no-store, must-revalidate',
                'Pragma': 'no-cache',
                'Expires': '0'
            }
        });

        if (response.ok) {
            const data = await response.json();

            if (data.success) {
                const targetVersion = data.photoVersion;

                let photoReady = false;
                let maxRetries = 20;
                let retryCount = 0;

                while (!photoReady && retryCount < maxRetries) {
                    const statusData = await checkPhotoStatus(targetVersion);
                    if (statusData) {
                        if (statusData.photoVersion >= targetVersion && statusData.newPhotoReady) {
                            photoReady = true;
                        } else if (!statusData.isTakingPhoto) {
                            await new Promise(resolve => setTimeout(resolve, 200));
                        }
                    }
                    if (!photoReady) {
                        await new Promise(resolve => setTimeout(resolve, 300));
                        retryCount++;
                    }
                }

                statusElement.textContent = '图片加载中...';
                document.getElementById('photo-size').textContent = formatBytes(data.photoSize);
                document.getElementById('photo-time').textContent = new Date().toLocaleString();

                placeholder.style.display = 'block';
                imageContainer.style.display = 'none';
                photoInfo.style.display = 'none';

                const uniqueId = Date.now() + '-' + Math.random().toString(36).substr(2, 9);
                const photoUrl = '/api/last-photo?t=' + uniqueId + '&v=' + targetVersion + '&nocache=' + uniqueId;
                
                let loadTimeout = null;
                let loadComplete = false;
                
                loadTimeout = setTimeout(() => {
                    if (!loadComplete) {
                        loadComplete = true;
                        statusElement.textContent = '图片加载超时';
                    }
                }, 15000);
                
                try {
                    const imgResponse = await fetch(photoUrl, {
                        cache: 'no-store',
                        headers: {
                            'Cache-Control': 'no-cache, no-store, must-revalidate',
                            'Pragma': 'no-cache'
                        }
                    });
                    
                    if (!imgResponse.ok) {
                        throw new Error('HTTP ' + imgResponse.status);
                    }
                    
                    const blob = await imgResponse.blob();
                    
                    if (loadComplete) {
                        URL.revokeObjectURL(URL.createObjectURL(blob));
                        return;
                    }
                    
                    const blobUrl = URL.createObjectURL(blob);
                    
                    if (img.dataset.blobUrl) {
                        URL.revokeObjectURL(img.dataset.blobUrl);
                    }
                    img.dataset.blobUrl = blobUrl;
                    
                    img.onload = () => {
                        if (!loadComplete) {
                            loadComplete = true;
                            if (loadTimeout) clearTimeout(loadTimeout);
                            placeholder.style.display = 'none';
                            imageContainer.style.display = 'block';
                            photoInfo.style.display = 'flex';
                            statusElement.textContent = '拍照成功';
                        }
                    };
                    
                    img.onerror = () => {
                        if (!loadComplete) {
                            loadComplete = true;
                            if (loadTimeout) clearTimeout(loadTimeout);
                            statusElement.textContent = '图片加载失败';
                        }
                    };
                    
                    img.src = blobUrl;
                    
                } catch (e) {
                    if (!loadComplete) {
                        loadComplete = true;
                        if (loadTimeout) clearTimeout(loadTimeout);
                        statusElement.textContent = '图片获取失败: ' + e.message;
                    }
                }

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

// 应用相机设置
async function applyCameraSettings() {
    const btn = document.getElementById('apply-camera-btn');
    const statusEl = document.getElementById('camera-settings-status');

    btn.disabled = true;
    showMessage(statusEl, '正在应用...', 'info');

    const settings = {
        frameSize: parseInt(document.getElementById('frame-size').value),
        jpegQuality: parseInt(document.getElementById('jpeg-quality').value),
        wbMode: parseInt(document.getElementById('wb-mode').value),
        specialEffect: parseInt(document.getElementById('special-effect').value),
        contrast: parseInt(document.getElementById('contrast').value),
        saturation: parseInt(document.getElementById('saturation').value),
        brightness: parseInt(document.getElementById('brightness').value),
        streamFps: parseInt(document.getElementById('stream-fps').value),
        streamFrameSize: parseInt(document.getElementById('stream-frame-size').value)
    };

    try {
        let response = null;
        for (let attempt = 0; attempt < 5; attempt++) {
            response = await fetch('/api/camera/set-all', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(settings)
            });
            if (response.status !== 409) break;
            await new Promise(r => setTimeout(r, 600));
        }

        if (response.ok) {
            const data = await response.json();
            if (data.success) {
                showMessage(statusEl, '相机设置已应用', 'success');
                if (monitorActive) {
                    await stopMonitor();
                    await startMonitor();
                }
                await loadCameraStatus();
            } else {
                showMessage(statusEl, '设置应用失败: ' + (data.error || '未知错误'), 'error');
            }
        } else if (response.status === 409) {
            showMessage(statusEl, '相机忙碌，请稍后重试', 'error');
        } else {
            try {
                const data = await response.json();
                showMessage(statusEl, '设置应用失败: ' + (data.error || '相机初始化失败'), 'error');
            } catch (_) {
                showMessage(statusEl, '请求失败', 'error');
            }
        }
    } catch (e) {
        showMessage(statusEl, '错误: ' + e.toString(), 'error');
        console.error('Apply camera settings failed:', e);
    } finally {
        btn.disabled = false;
    }
}

// 保存系统配置
async function saveConfig() {
    const saveBtn = document.getElementById('save-config-btn');
    const statusElement = document.getElementById('save-status');

    saveBtn.disabled = true;
    showMessage(statusElement, '正在保存...', 'info');

    // 收集WiFi列表
    currentConfig.wifiList = [];
    for (let i = 0; i < MAX_WIFI_SSIDS; i++) {
        currentConfig.wifiList.push({
            ssid: document.getElementById(`wifi-ssid-${i}`).value,
            pass: document.getElementById(`wifi-pass-${i}`).value
        });
    }
    
    currentConfig.postServer = document.getElementById('post-server').value;
    currentConfig.postUsePut = document.getElementById('post-use-put').checked;

    // 同时保存相机参数
    currentConfig.jpegQuantity = parseInt(document.getElementById('jpeg-quality').value);
    currentConfig.frameSize = parseInt(document.getElementById('frame-size').value);
    currentConfig.streamFps = parseInt(document.getElementById('stream-fps').value);
    currentConfig.streamFrameSize = parseInt(document.getElementById('stream-frame-size').value);
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

// ===== 监控流功能 (WebSocket) =====

function renderJpegFrame(blob, sizeBytes) {
    const now = performance.now();
    frameCount++;
    fpsCounter.frames++;
    streamTotalBytes += sizeBytes;
    if (now - fpsCounter.lastTs >= 1000 && fpsCounter.frames > 0) {
        fpsCounter.fps = (fpsCounter.frames * 1000 / (now - fpsCounter.lastTs)).toFixed(1);
        fpsCounter.frames = 0;
        fpsCounter.lastTs = now;
        document.getElementById('actual-fps').textContent = fpsCounter.fps + ' fps';
        document.getElementById('frame-count').textContent = frameCount;
        document.getElementById('stream-bytes').textContent = formatBytes(streamTotalBytes);
        document.getElementById('stream-fps-display').textContent = 'FPS: ' + (fpsCounter.fps || '--');
    }

    if (now - lastRenderTs < currentFrameIntervalMs) return;
    lastRenderTs = now;

    const url = URL.createObjectURL(blob);
    const img = document.getElementById('stream-image');
    if (currentBlobUrl) URL.revokeObjectURL(currentBlobUrl);
    currentBlobUrl = url;
    img.src = url;

    const tsEl = document.getElementById('stream-timestamp');
    if (tsEl) tsEl.textContent = 'TS: ' + Date.now() + ' ms';
}

async function startMonitor() {
    if (monitorActive) {
        return;
    }
    monitorActive = true;

    const statusDot = document.getElementById('monitor-status-dot');
    const statusText = document.getElementById('monitor-status');
    const startBtn = document.getElementById('monitor-start-btn');
    const stopBtn = document.getElementById('monitor-stop-btn');
    const placeholder = document.getElementById('stream-placeholder');
    const wrapper = document.getElementById('stream-wrapper');
    const stats = document.getElementById('stream-stats');

    startBtn.disabled = true;
    statusDot.className = 'status-dot busy';
    statusText.textContent = '正在启动相机...';

    frameCount = 0;
    streamTotalBytes = 0;
    lastRenderTs = 0;
    fpsCounter = { frames: 0, lastTs: performance.now(), fps: 0 };

    try {
        const resp = await fetch('/api/camera/stream-start', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' }
        });
        if (!resp.ok) {
            throw new Error('stream-start failed: HTTP ' + resp.status);
        }
        const data = await resp.json();
        currentFrameIntervalMs = data.frameIntervalMs || 50;

        const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
        const wsUrl = proto + '//' + location.host + '/ws/camera/stream';
        const ws = new WebSocket(wsUrl);
        streamWs = ws;
        ws.binaryType = 'blob';

        ws.onopen = () => {
            ws.send('start');

            statusText.textContent = '监控中';
            statusDot.className = 'status-dot';
            placeholder.style.display = 'none';
            wrapper.style.display = 'block';
            stats.style.display = 'flex';
            stopBtn.disabled = false;
        };

        ws.onmessage = (evt) => {
            if (!monitorActive) return;
            if (evt.data instanceof Blob) {
                renderJpegFrame(evt.data, evt.data.size);
            }
        };

        ws.onerror = (e) => {
            console.error('WS error:', e);
        };

        ws.onclose = () => {
            streamWs = null;
            if (monitorActive) {
                statusDot.className = 'status-dot offline';
                statusText.textContent = '连接断开';
                startBtn.disabled = false;
                stopBtn.disabled = true;
                monitorActive = false;
            }
        };
    } catch (e) {
        console.error('Start monitor failed:', e);
        statusDot.className = 'status-dot offline';
        statusText.textContent = '启动失败: ' + e.message;
        monitorActive = false;
        startBtn.disabled = false;
    }
}

async function stopMonitor() {
    if (!monitorActive) return;
    monitorActive = false;

    const statusDot = document.getElementById('monitor-status-dot');
    const statusText = document.getElementById('monitor-status');
    const startBtn = document.getElementById('monitor-start-btn');
    const stopBtn = document.getElementById('monitor-stop-btn');
    const placeholder = document.getElementById('stream-placeholder');
    const wrapper = document.getElementById('stream-wrapper');
    const stats = document.getElementById('stream-stats');

    if (streamWs) {
        try { streamWs.close(); } catch (e) {}
        streamWs = null;
    }

    if (currentBlobUrl) {
        URL.revokeObjectURL(currentBlobUrl);
        currentBlobUrl = null;
    }

    statusDot.className = 'status-dot offline';
    statusText.textContent = '已停止';
    startBtn.disabled = false;
    stopBtn.disabled = true;
    wrapper.style.display = 'none';
    placeholder.style.display = 'flex';
    stats.style.display = 'none';

    const tsEl = document.getElementById('stream-timestamp');
    if (tsEl) tsEl.textContent = 'TS: --';

    try {
        await fetch('/api/camera/stream-stop', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' }
        });
    } catch (e) {
        console.error('Stream stop failed:', e);
    }
}

// 页面卸载时清理
window.addEventListener('beforeunload', () => {
    if (monitorActive) {
        stopMonitor();
    }
    if (memoryRefreshTimer) {
        clearInterval(memoryRefreshTimer);
    }
});

// 工具函数
function showMessage(element, text, type) {
    element.textContent = text;
    element.className = 'message ' + type;
    element.style.display = 'block';
}

function formatBytes(bytes) {
    if (bytes === 0) return '0 B';
    const k = 1024;
    const sizes = ['B', 'KB', 'MB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
}