document.addEventListener('DOMContentLoaded', function() {
    const form = document.getElementById('configForm');
    const messageDiv = document.getElementById('message');
    const saveBtn = document.getElementById('saveBtn');
    
    // Load current configuration
    loadConfig();
    
    form.addEventListener('submit', async function(e) {
        e.preventDefault();
        await saveConfig();
    });
    
    async function loadConfig() {
        try {
            const response = await fetch('/api/config');
            const config = await response.json();
            
            if (config) {
                populateForm(config);
            }
        } catch (error) {
            console.error('Failed to load config:', error);
            showMessage('加载配置失败', 'error');
        }
    }
    
    function populateForm(config) {
        document.getElementById('wifiSsid').value = config.wifiSsid || '';
        document.getElementById('wifiPass').value = config.wifiPass || '';
        document.getElementById('postServer').value = config.postServer || '';
        document.getElementById('postPort').value = config.postPort || 8080;
        document.getElementById('postInterval').value = config.postInterval || 5;
        document.getElementById('jpegQuantity').value = config.jpegQuantity || 12;
        document.getElementById('frameSize').value = config.frameSize || 7;
    }
    
    async function saveConfig() {
        const config = {
            wifiSsid: document.getElementById('wifiSsid').value,
            wifiPass: document.getElementById('wifiPass').value,
            postServer: document.getElementById('postServer').value,
            postPort: parseInt(document.getElementById('postPort').value),
            postInterval: parseInt(document.getElementById('postInterval').value),
            jpegQuantity: parseInt(document.getElementById('jpegQuantity').value),
            frameSize: parseInt(document.getElementById('frameSize').value),
            startPoster: 'no',
            waitApFirst: 'no',
            nickname: '5mpCamera',
            timeZone: 'GMT+0'
        };
        
        try {
            saveBtn.disabled = true;
            saveBtn.textContent = '保存中...';
            messageDiv.textContent = '';
            messageDiv.className = 'message';
            
            const response = await fetch('/api/config', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify(config)
            });
            
            if (response.ok) {
                showMessage('保存成功！设备即将重启...', 'success');
                setTimeout(() => {
                    window.location.reload();
                }, 2000);
            } else {
                showMessage('保存失败', 'error');
            }
        } catch (error) {
            console.error('Failed to save config:', error);
            showMessage('保存失败', 'error');
        } finally {
            saveBtn.disabled = false;
            saveBtn.textContent = '保存并重启';
        }
    }
    
    function showMessage(text, type) {
        messageDiv.textContent = text;
        messageDiv.className = 'message ' + type;
    }
});