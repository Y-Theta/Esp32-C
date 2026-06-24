// 全局变量
let currentConfig = {
    wifiSsid: '',
    wifiPass: '',
    postServer: '',
    postPort: 8080,
    postInterval: 60,
    jpegQuantity: 1,
    frameSize: 10,
    startPoster: 'off',
    waitApFirst: 'off',
    nickname: '5mpCamera',
    timeZone: 'GMT+0'
};

let isTakingPhoto = false;

// 初始化
document.addEventListener('DOMContentLoaded', () => {
    initTabs();
    loadConfig();
    initButtons();
});

// Tab 导航
function initTabs() {
    const tabs = document.querySelectorAll('.tab');
    const tabPanes = document.querySelectorAll('.tab-pane');
    
    tabs.forEach(tab => {
        tab.addEventListener('click', () => {
            // 移除所有激活状态
            tabs.forEach(t => t.classList.remove('active'));
            tabPanes.forEach(p => p.classList.remove('active'));
            
            // 添加当前激活状态
            tab.classList.add('active');
            const targetId = tab.getAttribute('data-tab') + '-tab';
            document.getElementById(targetId).classList.add('active');
        });
    });
}

// 加载配置
async function loadConfig() {
    try {
        const response = await fetch('/api/config');
        if (response.ok) {
            currentConfig = await response.json();
            
            // 填充表单
            document.getElementById('wifi-ssid').value = currentConfig.wifiSsid || '';
            document.getElementById('wifi-password').value = currentConfig.wifiPass || '';
            document.getElementById('server-address').value = currentConfig.postServer || '';
            document.getElementById('server-port').value = currentConfig.postPort || 8080;
            document.getElementById('upload-interval').value = currentConfig.postInterval || 60;
            
            // 设置 JPEG 质量
            const qualitySelect = document.getElementById('jpeg-quality');
            for (let i = 0; i < qualitySelect.options.length; i++) {
                if (parseInt(qualitySelect.options[i].value) === currentConfig.jpegQuantity) {
                    qualitySelect.selectedIndex = i;
                    break;
                }
            }
            
            // 设置 Frame Size
            const frameSelect = document.getElementById('frame-size');
            for (let i = 0; i < frameSelect.options.length; i++) {
                if (parseInt(frameSelect.options[i].value) === currentConfig.frameSize) {
                    frameSelect.selectedIndex = i;
                    break;
                }
            }
            
            // 设置其他配置
            document.getElementById('nickname').value = currentConfig.nickname || '5mpCamera';
            document.getElementById('timezone').value = currentConfig.timeZone || 'GMT+0';
            
            console.log('Config loaded:', currentConfig);
        }
    } catch (e) {
        console.error('Failed to load config:', e);
    }
}

// 按钮事件
function initButtons() {
    // 拍照按钮
    document.getElementById('capture-btn').addEventListener('click', async () => {
        await capturePhoto();
    });
    
    // 保存配置按钮
    document.getElementById('save-config-btn').addEventListener('click', async () => {
        await saveConfig();
    });
    
    // 连接 STA 模式按钮
    document.getElementById('connect-sta-btn').addEventListener('click', async () => {
        await connectToSTA();
    });
}

// 拍照功能
async function capturePhoto() {
    if (isTakingPhoto) return;
    isTakingPhoto = true;
    
    const statusElement = document.getElementById('capture-status');
    const imageContainer = document.getElementById('captured-image');
    const captureBtn = document.getElementById('capture-btn');
    
    // 更新状态
    captureBtn.disabled = true;
    statusElement.textContent = '正在拍照...';
    imageContainer.style.display = 'none';
    
    try {
        const response = await fetch('/api/take-photo', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            }
        });
        
        if (response.ok) {
            const data = await response.json();
            
            if (data.success) {
                statusElement.textContent = `拍照成功！${data.photoSize} 字节`;
                
                // 延迟一下再显示图片（防止缓存问题）
                await new Promise(resolve => setTimeout(resolve, 300));
                
                // 显示图片，加上时间戳防缓存
                const timestamp = Date.now();
                const img = document.getElementById('captured-photo');
                img.src = data.photoUrl + '?t=' + timestamp;
                img.onload = () => {
                    imageContainer.style.display = 'block';
                };
                img.onerror = () => {
                    statusElement.textContent = '图片加载失败';
                };
                
                console.log('Photo captured:', data.photoUrl);
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
    }
}

// 保存配置
async function saveConfig() {
    const saveBtn = document.getElementById('save-config-btn');
    const statusElement = document.getElementById('save-status');
    
    saveBtn.disabled = true;
    statusElement.textContent = '正在保存...';
    
    // 更新配置
    currentConfig.wifiSsid = document.getElementById('wifi-ssid').value;
    currentConfig.wifiPass = document.getElementById('wifi-password').value;
    currentConfig.postServer = document.getElementById('server-address').value;
    currentConfig.postPort = parseInt(document.getElementById('server-port').value);
    currentConfig.postInterval = parseInt(document.getElementById('upload-interval').value);
    currentConfig.jpegQuantity = parseInt(document.getElementById('jpeg-quality').value);
    currentConfig.frameSize = parseInt(document.getElementById('frame-size').value);
    currentConfig.nickname = document.getElementById('nickname').value;
    currentConfig.timeZone = document.getElementById('timezone').value;
    
    try {
        const response = await fetch('/api/config', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify(currentConfig)
        });
        
        if (response.ok) {
            const data = await response.json();
            if (data.success) {
                statusElement.textContent = '保存成功！正在重启...';
                console.log('Config saved');
            }
        } else {
            statusElement.textContent = '保存失败';
        }
    } catch (e) {
        statusElement.textContent = '错误: ' + e.toString();
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
            headers: {
                'Content-Type': 'application/json'
            }
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