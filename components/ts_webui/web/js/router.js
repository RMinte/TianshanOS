/**
 * TianshanOS SPA Router
 * 简单的 Hash 路由器，带认证保护
 */

class Router {
    constructor() {
        this.routes = {};
        this.currentPage = null;
        
        // 需要 root 权限的页面（终端、自动化、指令）
        this.rootOnlyPages = ['/terminal', '/automation', '/commands'];
        
        // 需要登录的页面（除了登录页面本身，所有页面都需要）
        this.publicPages = [];  // 暂时没有公开页面
        
        window.addEventListener('hashchange', () => this.navigate());
        window.addEventListener('load', () => this.navigate());
    }
    
    register(path, loader) {
        this.routes[path] = loader;
    }
    
    /**
     * 检查页面访问权限
     * @returns {object} { allowed: boolean, reason: string }
     */
    checkAccess(path) {
        // 检查是否已登录
        if (!api.isLoggedIn()) {
            return { allowed: false, reason: 'not_logged_in' };
        }
        
        // 检查 root 专属页面
        if (this.rootOnlyPages.includes(path)) {
            if (!api.isRoot()) {
                return { allowed: false, reason: 'root_required' };
            }
        }
        
        return { allowed: true };
    }
    
    navigate(path = null) {
        if (path) {
            window.location.hash = path;
            return;
        }
        
        let hash = window.location.hash.slice(1) || '/';
        
        // 页面切换前的清理 - 停止定时器等
        if (typeof stopDeviceStateMonitor === 'function') {
            stopDeviceStateMonitor();
        }
        // 取消系统页快捷操作定时器，避免切走后仍触发 refreshQuickActions
        if (typeof stopSystemPageTimers === 'function') {
            stopSystemPageTimers();
        }
        // 离开终端页时主动断开 WebSocket，避免 1006 异常关闭
        if (typeof destroyWebTerminal === 'function') {
            destroyWebTerminal();
        }
        
        // 检查访问权限
        const access = this.checkAccess(hash);
        if (!access.allowed) {
            if (access.reason === 'not_logged_in') {
                // 未登录，显示登录框
                showLoginModal();
                return;
            }
            if (access.reason === 'root_required') {
                // 需要 root 权限
                showToast(t('toast.rootRequired'), 'error');
                window.location.hash = '/';  // 重定向到首页
                return;
            }
        }
        
        // 更新导航高亮
        document.querySelectorAll('.nav-link').forEach(link => {
            const href = link.getAttribute('href');
            if (href === '#' + hash || (hash === '/' && href === '#/')) {
                link.classList.add('active');
            } else {
                link.classList.remove('active');
            }
        });
        
        // 更新导航项的可见性（根据权限）
        this.updateNavVisibility();
        
        // 查找路由
        const loader = this.routes[hash] || this.routes['/'];
        if (loader) {
            this.currentPage = loader;
            loader();
        }
    }
    
    /**
     * 获取当前 hash 对应的 loader，供语言切换后重新渲染当前页
     * @returns {function|null} 当前页的 loader 或 null
     */
    getCurrentLoader() {
        const hash = window.location.hash.slice(1) || '/';
        const access = this.checkAccess(hash);
        if (!access.allowed) return null;
        return this.routes[hash] || this.routes['/'] || null;
    }
    
    /**
     * 根据权限更新导航菜单可见性
     */
    updateNavVisibility() {
        const isRoot = api.isRoot();
        const isLoggedIn = api.isLoggedIn();
        
        // 根据权限显示/隐藏导航项
        document.querySelectorAll('.nav-link').forEach(link => {
            // 检查 data-requires-root 属性
            if (link.hasAttribute('data-requires-root')) {
                if (isLoggedIn && isRoot) {
                    link.style.display = '';
                } else {
                    link.style.display = 'none';
                }
            }
        });
    }
}

const router = new Router();
