/**
 * TianShanOS API Client
 */

const API_BASE = '/api/v1';

class TianShanAPI {
    constructor() {
        this.token = localStorage.getItem('token');
    }
    
    async request(endpoint, method = 'GET', data = null) {
        const headers = {
            'Content-Type': 'application/json'
        };
        
        if (this.token) {
            headers['Authorization'] = `Bearer ${this.token}`;
        }
        
        const options = {
            method,
            headers
        };
        
        if (data && method !== 'GET') {
            options.body = JSON.stringify(data);
        }
        
        try {
            const response = await fetch(`${API_BASE}${endpoint}`, options);
            const json = await response.json();
            
            if (!response.ok) {
                throw new Error(json.error || 'Request failed');
            }
            
            return json;
        } catch (error) {
            console.error(`API Error: ${endpoint}`, error);
            throw error;
        }
    }
    
    // Auth
    async login(username, password) {
        const result = await this.request('/auth/login', 'POST', { username, password });
        if (result.token) {
            this.token = result.token;
            localStorage.setItem('token', result.token);
        }
        return result;
    }
    
    async logout() {
        try {
            await this.request('/auth/logout', 'POST');
        } finally {
            this.token = null;
            localStorage.removeItem('token');
        }
    }
    
    isLoggedIn() {
        return !!this.token;
    }
    
    // System
    async getSystemInfo() {
        return this.request('/system/info');
    }
    
    async getMemoryInfo() {
        return this.request('/system/memory');
    }
    
    async getTasks() {
        return this.request('/system/tasks');
    }
    
    async reboot() {
        return this.request('/system/reboot', 'POST');
    }
    
    // Config
    async getConfig(key) {
        return this.request(`/config/get?key=${encodeURIComponent(key)}`);
    }
    
    async setConfig(key, value) {
        return this.request('/config/set', 'POST', { key, value });
    }
    
    async listConfig(prefix = '') {
        return this.request(`/config/list?prefix=${encodeURIComponent(prefix)}`);
    }
    
    // LED
    async getLedDevices() {
        return this.request('/led/devices');
    }
    
    async setLedBrightness(device, brightness) {
        return this.request('/led/brightness', 'POST', { device, brightness });
    }
    
    async setLedEffect(device, effect) {
        return this.request('/led/effect', 'POST', { device, effect });
    }
    
    async setLedColor(device, color) {
        return this.request('/led/color', 'POST', { device, color });
    }
    
    // Network
    async getNetworkStatus() {
        return this.request('/network/status');
    }
    
    async getWifiNetworks() {
        return this.request('/network/wifi/scan');
    }
    
    async connectWifi(ssid, password) {
        return this.request('/network/wifi/connect', 'POST', { ssid, password });
    }
    
    // Device
    async getDeviceStatus() {
        return this.request('/device/status');
    }
    
    async setDevicePower(device, on) {
        return this.request('/device/power', 'POST', { device, on });
    }
    
    async getFanStatus() {
        return this.request('/device/fan/status');
    }
    
    async setFanSpeed(fan, speed) {
        return this.request('/device/fan/speed', 'POST', { fan, speed });
    }
}

// WebSocket client
class TianShanWS {
    constructor(onMessage) {
        this.ws = null;
        this.onMessage = onMessage;
        this.reconnectTimer = null;
    }
    
    connect() {
        const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
        this.ws = new WebSocket(`${protocol}//${location.host}/ws`);
        
        this.ws.onopen = () => {
            console.log('WebSocket connected');
            this.send({ type: 'subscribe' });
        };
        
        this.ws.onmessage = (event) => {
            try {
                const data = JSON.parse(event.data);
                if (this.onMessage) {
                    this.onMessage(data);
                }
            } catch (e) {
                console.error('WS parse error:', e);
            }
        };
        
        this.ws.onclose = () => {
            console.log('WebSocket disconnected');
            this.reconnect();
        };
        
        this.ws.onerror = (error) => {
            console.error('WebSocket error:', error);
        };
    }
    
    reconnect() {
        if (this.reconnectTimer) return;
        this.reconnectTimer = setTimeout(() => {
            this.reconnectTimer = null;
            this.connect();
        }, 5000);
    }
    
    send(data) {
        if (this.ws && this.ws.readyState === WebSocket.OPEN) {
            this.ws.send(JSON.stringify(data));
        }
    }
    
    disconnect() {
        if (this.reconnectTimer) {
            clearTimeout(this.reconnectTimer);
            this.reconnectTimer = null;
        }
        if (this.ws) {
            this.ws.close();
            this.ws = null;
        }
    }
}

// Export
window.api = new TianShanAPI();
window.TianShanWS = TianShanWS;
