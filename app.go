package main

import (
	"context"
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"runtime"
)

// App struct
type App struct {
	ctx context.Context
}

// Структуры для парсинга JSON
type Config struct {
	Software []Software `json:"software"`
}

type Software struct {
	Name            string `json:"name"`
	Cat             string `json:"cat"`
	Exe             string `json:"exe"`
	Flag            string `json:"flag"`
	InstallDflt     bool   `json:"install_dflt"`
	SilentSupported bool   `json:"silent_supported"`
	SilentDflt      bool   `json:"silent_dflt"`
	SilentArgs      string `json:"silent_args"`
	NormalArgs      string `json:"normal_args"`
}

// NewApp creates a new App application struct
func NewApp() *App {
	return &App{}
}

// startup is called when the app starts
func (a *App) startup(ctx context.Context) {
	a.ctx = ctx
}

// GetSystemInfo возвращает разрядность системы (x86 или x64)
func (a *App) GetSystemInfo() string {
	if runtime.GOARCH == "amd64" {
		return "x64"
	}
	return "x86"
}

// LoadConfig читает config.json и отправляет его в интерфейс
func (a *App) LoadConfig() (Config, error) {
	var config Config

	// В режиме разработки (на Linux) мы просто читаем файл из корня
	file, err := os.ReadFile("config.json")
	if err != nil {
		return config, fmt.Errorf("ошибка чтения конфига: %v", err)
	}

	err = json.Unmarshal(file, &config)
	if err != nil {
		return config, fmt.Errorf("ошибка парсинга JSON: %v", err)
	}

	return config, nil
}

// RunInstaller запускает exe файл с нужными аргументами
func (a *App) RunInstaller(exePath string, args string) string {
	fmt.Printf("Запуск: %s %s\n", exePath, args)

	// Если мы тестируем на Linux, просто возвращаем фейковый успех
	if runtime.GOOS == "linux" {
		return fmt.Sprintf("[Linux Mock] Успешно 'установлено': %s", exePath)
	}

	// Реальный запуск для Windows
	cmd := exec.Command(exePath, args)
	err := cmd.Run()
	if err != nil {
		return fmt.Sprintf("Ошибка установки %s: %v", exePath, err)
	}
	return fmt.Sprintf("Успешно: %s", exePath)
}
