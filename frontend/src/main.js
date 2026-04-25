import { LoadConfig, GetSystemInfo, RunInstaller } from '../wailsjs/go/main/App';

let softwareData = [];
let systemArch = "";

// Инициализация при загрузке
window.onload = async () => {
    try {
        // Получаем разрядность из Go
        systemArch = await GetSystemInfo();
        document.getElementById('sys-info').innerText = `[${systemArch}]`;

        // Получаем конфиг из Go
        const config = await LoadConfig();
        softwareData = config.software;
        renderList();
    } catch (err) {
        document.getElementById('status-log').innerText = "Ошибка загрузки: " + err;
    }
};

// Отрисовка списка программ
function renderList() {
    const container = document.getElementById('app-list');
    container.innerHTML = "";

    softwareData.forEach((app, index) => {
        // Проверка разрядности
        const isDisabled = (app.flag === "x64" && systemArch !== "x64");

        const item = document.createElement('div');
        item.className = `app-item ${isDisabled ? 'disabled' : ''}`;

        item.innerHTML = `
            <div>
                <label>
                    <input type="checkbox" id="install-${index}" 
                           ${app.install_dflt && !isDisabled ? 'checked' : ''} 
                           ${isDisabled ? 'disabled' : ''}>
                    <strong style="font-size: 18px;">${app.name}</strong> 
                    <span style="color: #888; font-size: 12px;">[${app.cat}]</span>
                </label>
                ${isDisabled ? '<div style="color: #ff4444; font-size: 12px; margin-top: 5px;">Не поддерживается разрядность системы</div>' : ''}
            </div>
            <div>
                ${app.silent_supported ? `
                <label style="font-size: 14px; color: #ccc;">
                    <input type="checkbox" id="silent-${index}" 
                           ${app.silent_dflt ? 'checked' : ''} 
                           ${isDisabled ? 'disabled' : ''}> Тихая
                </label>
                ` : '<span style="color: #555; font-size: 12px;">Тихая недоступна</span>'}
            </div>
        `;
        container.appendChild(item);
    });
}

// Запуск установки (вешаем на window, чтобы кнопка в HTML видела функцию)
window.startInstall = async () => {
    const log = document.getElementById('status-log');

    for (let i = 0; i < softwareData.length; i++) {
        const app = softwareData[i];
        const installCheckbox = document.getElementById(`install-${i}`);

        if (installCheckbox && installCheckbox.checked) {
            const silentCheckbox = document.getElementById(`silent-${i}`);
            const isSilent = silentCheckbox ? silentCheckbox.checked : false;

            const args = isSilent ? app.silent_args : app.normal_args;

            log.innerText = `Устанавливается: ${app.name}...`;

            // Вызов функции Go
            const result = await RunInstaller(app.exe, args);
            console.log(result);
        }
    }
    log.innerText = "Установка завершена!";
};