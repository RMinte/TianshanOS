/**
 * TianShanOS WebUI 国际化（i18n）系统
 * 
 * 使用方法：
 *   t('key')           - 获取翻译文本
 *   t('key', {name})   - 带参数的翻译 "Hello {name}" -> "Hello Tom"
 *   setLanguage('en')  - 切换语言
 *   getLanguage()      - 获取当前语言
 */

const i18n = (function() {
    // 当前语言
    let currentLang = 'zh-CN';
    
    // 语言包存储
    const languages = {};
    
    // 支持的语言列表
    const supportedLanguages = {
        'zh-CN': { name: '简体中文', flag: '' },
        'en-US': { name: 'English', flag: '' }
    };
    
    /**
     * 注册语言包
     */
    function registerLanguage(code, translations) {
        languages[code] = translations;
    }
    
    /**
     * 设置当前语言
     */
    function setLanguage(lang) {
        if (languages[lang]) {
            currentLang = lang;
            localStorage.setItem('ts_language', lang);
            document.documentElement.lang = lang;
            // 触发语言变更事件
            window.dispatchEvent(new CustomEvent('languageChanged', { detail: { language: lang } }));
            return true;
        }
        console.warn(`Language '${lang}' not found`);
        return false;
    }
    
    /**
     * 获取当前语言
     */
    function getLanguage() {
        return currentLang;
    }
    
    /**
     * 获取翻译文本
     * @param {string} key - 翻译键，支持点号分隔的嵌套键 "menu.home"
     * @param {object} params - 替换参数 {name: 'Tom'} 替换 {name}
     * @returns {string} 翻译后的文本
     */
    function translate(key, params = {}) {
        const translations = languages[currentLang] || languages['zh-CN'] || {};
        
        // 支持嵌套键 "menu.home" -> translations.menu.home
        let value = key.split('.').reduce((obj, k) => obj && obj[k], translations);
        
        // 如果没找到，尝试用英文
        if (value === undefined && currentLang !== 'en-US' && languages['en-US']) {
            value = key.split('.').reduce((obj, k) => obj && obj[k], languages['en-US']);
        }
        
        // 仍然没找到，返回键名（开发时便于发现未翻译的文本）
        if (value === undefined) {
            console.debug(`Missing translation: ${key}`);
            return key;
        }
        
        // 替换参数 {name} -> params.name
        if (typeof value === 'string' && Object.keys(params).length > 0) {
            value = value.replace(/\{(\w+)\}/g, (match, paramKey) => {
                return params[paramKey] !== undefined ? params[paramKey] : match;
            });
        }
        
        return value;
    }
    
    /**
     * 初始化语言系统
     */
    function init() {
        // 从 localStorage 读取保存的语言偏好
        const savedLang = localStorage.getItem('ts_language');
        if (savedLang && languages[savedLang]) {
            currentLang = savedLang;
        } else {
            // 自动检测浏览器语言
            const browserLang = navigator.language || navigator.userLanguage;
            if (browserLang.startsWith('zh')) {
                currentLang = 'zh-CN';
            } else {
                currentLang = 'en-US';
            }
        }
        document.documentElement.lang = currentLang;
    }
    
    /**
     * 获取支持的语言列表
     */
    function getSupportedLanguages() {
        return supportedLanguages;
    }
    
    /**
     * 翻译 DOM 元素
     * 查找所有带有 data-i18n 属性的元素并翻译
     * @param {HTMLElement} root - 根元素，默认为 document
     */
    function translateDOM(root = document) {
        const elements = root.querySelectorAll('[data-i18n]');
        elements.forEach(el => {
            const key = el.getAttribute('data-i18n');
            const params = el.dataset.i18nParams ? JSON.parse(el.dataset.i18nParams) : {};
            el.textContent = translate(key, params);
        });
        
        // 翻译 placeholder
        const placeholders = root.querySelectorAll('[data-i18n-placeholder]');
        placeholders.forEach(el => {
            const key = el.getAttribute('data-i18n-placeholder');
            el.placeholder = translate(key);
        });
        
        // 翻译 title
        const titles = root.querySelectorAll('[data-i18n-title]');
        titles.forEach(el => {
            const key = el.getAttribute('data-i18n-title');
            el.title = translate(key);
        });
    }
    
    // 导出 API
    return {
        registerLanguage,
        setLanguage,
        getLanguage,
        translate,
        init,
        getSupportedLanguages,
        translateDOM
    };
})();

// 全局快捷函数
const t = i18n.translate;
const setLanguage = i18n.setLanguage;
const getLanguage = i18n.getLanguage;
const translateDOM = i18n.translateDOM;

// 语言切换菜单
function toggleLanguageMenu() {
    const menu = document.getElementById('lang-menu');
    if (!menu) return;
    
    if (menu.classList.contains('hidden')) {
        // 填充语言列表
        const langs = i18n.getSupportedLanguages();
        const currentLang = i18n.getLanguage();
        menu.innerHTML = Object.entries(langs).map(([code, info]) => `
            <div class="lang-menu-item${code === currentLang ? ' active' : ''}" onclick="selectLanguage('${code}')">
                <span class="lang-menu-name">${info.name}</span>
                ${code === currentLang ? '<span class="lang-menu-check">✓</span>' : ''}
            </div>
        `).join('');
        menu.classList.remove('hidden');
        
        // 点击外部关闭
        setTimeout(() => {
            document.addEventListener('click', closeLangMenuOnClickOutside);
        }, 10);
    } else {
        menu.classList.add('hidden');
    }
}

function closeLangMenuOnClickOutside(e) {
    const langSwitch = document.getElementById('lang-switch');
    if (langSwitch && !langSwitch.contains(e.target)) {
        const menu = document.getElementById('lang-menu');
        if (menu) menu.classList.add('hidden');
        document.removeEventListener('click', closeLangMenuOnClickOutside);
    }
}

function selectLanguage(lang) {
    if (i18n.setLanguage(lang)) {
        // 更新按钮显示
        const langs = i18n.getSupportedLanguages();
        const nameEl = document.getElementById('lang-name');
        if (nameEl && langs[lang]) {
            nameEl.textContent = langs[lang].name.split(' ')[0]; // 简短名称
        }
        // 翻译整个页面
        i18n.translateDOM();
        // 关闭菜单
        const menu = document.getElementById('lang-menu');
        if (menu) menu.classList.add('hidden');
    }
}

// 页面加载时初始化语言按钮，并翻译整页（使刷新后导航栏等按当前语言显示）
document.addEventListener('DOMContentLoaded', function() {
    i18n.init();
    const langs = i18n.getSupportedLanguages();
    const currentLang = i18n.getLanguage();
    const nameEl = document.getElementById('lang-name');
    if (nameEl && langs[currentLang]) {
        nameEl.textContent = currentLang === 'zh-CN' ? '中文' : 'EN';
    }
    i18n.translateDOM();
});
