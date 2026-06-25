// 全局配置
let currentConfig = {
    wifiSsid: '',
    wifiPass: '',
    postServer: '',
    postPort: 8080,
    postInterval: 60,
    jpegQuantity: 1,
    frameSize: 10,
    streamFps: 25,
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

// ===== 监控流相关 (HTTP 并发预取 + 前端缓冲队列) =====
// 生产者(requestFrame)维护 N 个并发请求，请求完成立即入队并补充新请求
// 消费者(displayLoop)按目标帧率从队列取帧显示
// 这样网络延迟被并发请求隐藏，帧率上限 ≈ min(相机实际帧率, 目标帧率)
let monitorActive = false;         // 监控是否激活
let displayTimer = null;           // 显示循环定时器（消费者）
let frameQueue = [];               // 已接收待显示的帧队列（缓冲区）
let pendingRequests = 0;           // 正在进行中的请求数（生产者）
let frameCount = 0;                // 已接收帧数
let streamTotalBytes = 0;          // 累计接收字节数
let fpsCounter = { frames: 0, lastTs: 0, fps: 0 };
let currentBlobUrl = null;         // 当前 img 的 blob URL，用于释放
let currentFrameIntervalMs = 50;   // 帧间隔，由配置决定
const MAX_PENDING_REQUESTS = 2;    // 最大并发请求数（流水线深度，隐藏网络延迟）
const MAX_QUEUE_SIZE = 3;          // 帧队列最大长度，避免内存堆积

// 更新内存状态显示
async function updateMemoryStatus() {
    try {
        const response = await fetch('/api/memory-status');
        if (response.ok) {
            const data = await response.json();
            
            // 更新 RAM
            document.getElementById('ram-used').textContent = formatBytes(data.ram.used);
            document.getElementById('ram-total').textContent = formatBytes(data.ram.total);
            
            // 更新 PSRAM
            document.getElementById('psram-used').textContent = formatBytes(data.psram.used);
            document.getElementById('psram-total').textContent = formatBytes(data.psram.total);
        }
    } catch (e) {
        console.error('Failed to update memory status:', e);
    }
}

// 初始化
document.addEventListener('DOMContentLoaded', () => {
    initTabs();
    initRangeInputs();
    initButtons();
    loadConfig();
    loadCameraStatus();
    
    // 立即更新一次内存状态
    updateMemoryStatus();
    
    // 每 3 秒刷新内存状态（推流时降频避免与帧请求竞争 HTTP 连接）
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

            // 从推流页签切换到其他页签时自动关闭推流
            if (currentActiveTab === 'monitor' && targetTab !== 'monitor') {
                stopMonitor();
            }
            // 切换到推流页签时不自动启动，需要手动点击按钮
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
    document.getElementById('connect-sta-btn').addEventListener('click', connectToSTA);
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

    // 先隐藏并清空旧图片 - 使用透明占位图避免触发请求
    placeholder.style.display = 'block';
    imageContainer.style.display = 'none';
    const img = document.getElementById('captured-photo');
    // 释放旧的 blob URL（如果存在）
    if (img.dataset.blobUrl) {
        URL.revokeObjectURL(img.dataset.blobUrl);
        delete img.dataset.blobUrl;
    }
    img.src = 'data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7'; // 1x1 透明 GIF

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
                console.log('Waiting for photo version:', targetVersion);

                // 轮询查询拍照状态，直到新照片准备好
                let photoReady = false;
                let maxRetries = 20; // 最多等待约6秒
                let retryCount = 0;

                while (!photoReady && retryCount < maxRetries) {
                    const statusData = await checkPhotoStatus(targetVersion);
                    if (statusData) {
                        if (statusData.photoVersion >= targetVersion && statusData.newPhotoReady) {
                            photoReady = true;
                            console.log('New photo ready, version:', statusData.photoVersion);
                        } else if (!statusData.isTakingPhoto) {
                            // 如果已经停止拍照但照片没准备好，多等一会儿
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

                // 保持隐藏图片容器，先不显示
                placeholder.style.display = 'block';
                imageContainer.style.display = 'none';
                photoInfo.style.display = 'none';

                // 使用 fetch + blob 方式获取图片，完全绕过浏览器缓存
                const uniqueId = Date.now() + '-' + Math.random().toString(36).substr(2, 9);
                const photoUrl = '/api/last-photo?t=' + uniqueId + '&v=' + targetVersion + '&nocache=' + uniqueId;
                
                let loadTimeout = null;
                let loadComplete = false;
                
                // 设置超时保护，防止卡死
                loadTimeout = setTimeout(() => {
                    if (!loadComplete) {
                        loadComplete = true;
                        statusElement.textContent = '图片加载超时';
                        console.error('Image load timeout');
                    }
                }, 15000); // 15秒超时
                
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
                    
                    // 创建 blob URL
                    const blobUrl = URL.createObjectURL(blob);
                    
                    // 释放之前的 blob URL
                    if (img.dataset.blobUrl) {
                        URL.revokeObjectURL(img.dataset.blobUrl);
                    }
                    img.dataset.blobUrl = blobUrl;
                    
                    // 设置图片加载回调
                    img.onload = () => {
                        if (!loadComplete) {
                            loadComplete = true;
                            if (loadTimeout) {
                                clearTimeout(loadTimeout);
                            }
                            // 图片完全加载后再展示
                            placeholder.style.display = 'none';
                            imageContainer.style.display = 'block';
                            photoInfo.style.display = 'flex';
                            statusElement.textContent = '拍照成功';
                            console.log('Image loaded successfully via blob');
                        }
                    };
                    
                    img.onerror = () => {
                        if (!loadComplete) {
                            loadComplete = true;
                            if (loadTimeout) {
                                clearTimeout(loadTimeout);
                            }
                            statusElement.textContent = '图片加载失败';
                            console.error('Image failed to load');
                        }
                    };
                    
                    // 设置 src 触发 onload
                    img.src = blobUrl;
                    
                } catch (e) {
                    if (!loadComplete) {
                        loadComplete = true;
                        if (loadTimeout) {
                            clearTimeout(loadTimeout);
                        }
                        statusElement.textContent = '图片获取失败: ' + e.message;
                        console.error('Image fetch failed:', e);
                    }
                }

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
        // 拍照期间后端会返回 409，自动重试避免用户看到失败提示
        let response = null;
        for (let attempt = 0; attempt < 5; attempt++) {
            response = await fetch('/api/camera/set-all', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(settings)
            });
            if (response.status !== 409) break;
            // 相机忙，等待 600ms 后重试
            await new Promise(r => setTimeout(r, 600));
        }

        if (response.ok) {
            const data = await response.json();
            if (data.success) {
                showMessage(statusEl, '相机设置已应用', 'success');
                // 如果正在推流，需要重启推流使帧率生效
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
            // 相机初始化失败时后端会返回 success=false
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

// ===== 监控流功能 (HTTP 并发预取 + 前端缓冲队列) =====

// 生产者：发起一个帧请求，完成后自动补充新请求（形成流水线）
// 多个 requestFrame 可并发执行，由 MAX_PENDING_REQUESTS 控制并发上限
async function requestFrame() {
    if (!monitorActive) return;
    if (pendingRequests >= MAX_PENDING_REQUESTS) return;
    // 队列已满时不再请求，由 displayLoop 消费后触发补充
    if (frameQueue.length >= MAX_QUEUE_SIZE) return;

    pendingRequests++;
    try {
        const uniqueId = Date.now() + '-' + Math.random().toString(36).substr(2, 9);
        const response = await fetch('/api/camera/stream-frame?t=' + uniqueId, {
            cache: 'no-store',
            headers: {
                'Cache-Control': 'no-cache, no-store, must-revalidate',
                'Pragma': 'no-cache'
            }
        });

        if (!response.ok) {
            throw new Error('HTTP ' + response.status);
        }

        const blob = await response.blob();
        streamTotalBytes += blob.size;
        frameCount++;

        // 读取后端附加的帧时间戳（毫秒），用于前端排序消除并发乱序
        const tsStr = response.headers.get('X-Frame-Timestamp');
        const timestamp = tsStr ? parseInt(tsStr, 10) : Date.now();

        // 入队（仅当还在监控且队列未满时）
        if (monitorActive && frameQueue.length < MAX_QUEUE_SIZE) {
            frameQueue.push({ blob, timestamp });

            // 更新 FPS 统计（基于接收到的帧，反映相机+网络实际能力）
            const now = performance.now();
            fpsCounter.frames++;
            const elapsed = now - fpsCounter.lastTs;
            if (elapsed >= 1000) {
                fpsCounter.fps = (fpsCounter.frames * 1000 / elapsed).toFixed(1);
                fpsCounter.frames = 0;
                fpsCounter.lastTs = now;
                document.getElementById('actual-fps').textContent = fpsCounter.fps + ' fps';
                document.getElementById('frame-count').textContent = frameCount;
                document.getElementById('stream-bytes').textContent = formatBytes(streamTotalBytes);
                document.getElementById('stream-fps-display').textContent = 'FPS: ' + (fpsCounter.fps || '--');
            }
        }
    } catch (e) {
        // 帧获取失败可能是临时错误，不停止，由流水线自动重试
        console.warn('Frame fetch failed:', e);
    } finally {
        pendingRequests--;
        // 请求完成后，如果还在监控，立即尝试补充（维持流水线深度）
        if (monitorActive) {
            requestFrame();
        }
    }
}

// 消费者：按目标帧率从队列取帧显示
// 队列空时保持上一帧，队列有积压时按固定节奏消费
function displayLoop() {
    if (!monitorActive) return;

    if (frameQueue.length > 0) {
        // 并发请求可能导致帧乱序到达，按时间戳升序排序后取最早的一帧展示
        frameQueue.sort((a, b) => a.timestamp - b.timestamp);
        const item = frameQueue.shift();
        const url = URL.createObjectURL(item.blob);
        const img = document.getElementById('stream-image');
        if (currentBlobUrl) {
            URL.revokeObjectURL(currentBlobUrl);
        }
        currentBlobUrl = url;
        img.src = url;

        // 更新时间戳显示
        const tsEl = document.getElementById('stream-timestamp');
        if (tsEl) tsEl.textContent = 'TS: ' + item.timestamp + ' ms';
    }
    // else: 队列空，保持上一帧显示（避免闪烁）

    // 队列有空间，触发生产者补充（维持流水线）
    if (frameQueue.length < MAX_QUEUE_SIZE && pendingRequests < MAX_PENDING_REQUESTS) {
        requestFrame();
    }

    displayTimer = setTimeout(displayLoop, currentFrameIntervalMs);
}

// 启动监控：HTTP轮询方式，不使用WebSocket
async function startMonitor() {
    if (monitorActive) {
        console.log('Monitor already active');
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

    // 重置统计和缓冲队列
    frameCount = 0;
    streamTotalBytes = 0;
    frameQueue = [];
    pendingRequests = 0;
    fpsCounter = { frames: 0, lastTs: performance.now(), fps: 0 };

    try {
        // 1. 通知后端启动推流模式
        const resp = await fetch('/api/camera/stream-start', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' }
        });
        if (!resp.ok) {
            throw new Error('stream-start failed: HTTP ' + resp.status);
        }
        const data = await resp.json();
        console.log('Stream start:', data);

        // 根据配置设置帧间隔
        currentFrameIntervalMs = data.frameIntervalMs || 50;

        statusText.textContent = '监控中';
        statusDot.className = 'status-dot';
        placeholder.style.display = 'none';
        wrapper.style.display = 'block';
        stats.style.display = 'flex';
        stopBtn.disabled = false;

        // 2. 启动显示循环（消费者，按目标帧率取帧显示）
        displayLoop();
        // 3. 启动 N 个并发请求填满流水线（生产者，隐藏网络延迟）
        for (let i = 0; i < MAX_PENDING_REQUESTS; i++) {
            requestFrame();
        }
        
    } catch (e) {
        console.error('Start monitor failed:', e);
        statusDot.className = 'status-dot offline';
        statusText.textContent = '启动失败: ' + e.message;
        monitorActive = false;
        startBtn.disabled = false;
    }
}

// 停止监控：清除定时器，通知后端停止
async function stopMonitor() {
    if (!monitorActive) {
        return;
    }
    monitorActive = false;

    const statusDot = document.getElementById('monitor-status-dot');
    const statusText = document.getElementById('monitor-status');
    const startBtn = document.getElementById('monitor-start-btn');
    const stopBtn = document.getElementById('monitor-stop-btn');
    const placeholder = document.getElementById('stream-placeholder');
    const wrapper = document.getElementById('stream-wrapper');
    const stats = document.getElementById('stream-stats');

    // 清除显示循环定时器
    if (displayTimer) {
        clearTimeout(displayTimer);
        displayTimer = null;
    }

    // 清空帧队列（pending 请求会因 monitorActive=false 自然停止入队）
    frameQueue = [];

    // 释放当前图像URL
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

    // 重置时间戳显示
    const tsEl = document.getElementById('stream-timestamp');
    if (tsEl) tsEl.textContent = 'TS: --';

    // 通知后端停止推流模式
    try {
        await fetch('/api/camera/stream-stop', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' }
        });
        console.log('Stream stop requested');
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