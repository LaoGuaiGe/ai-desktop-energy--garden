---
description: 自动获取配置并执行 ESP-IDF 编译与烧录
---

# 🚀 ESP-IDF 极速构建工作流 (Antigravity Turbo)

此工作流用于自动化执行编译、烧录和监控。

### 1. 环境预检查 🔍
读取 `.vscode/settings.json` 获取当前 IDF 路径和端口。

// turbo
### 2. 执行编译 🔨
使用 PowerShell 执行编译。

```powershell
$settings = Get-Content .vscode/settings.json | ConvertFrom-Json
$idfPath = $settings.idf.currentSetup
cmd /c "$idfPath\export.bat && idf.py build"
```

### 3. 结果验证 ✅
检查输出中是否包含 `Project build complete`。

// turbo
### 4. 自动烧录 ⚡
如果编译成功，则自动通过配置的 `idf.portWin` 端口进行烧录。

```powershell
$settings = Get-Content .vscode/settings.json | ConvertFrom-Json
$idfPath = $settings.idf.currentSetup
$port = $settings.idf.portWin
cmd /c "$idfPath\export.bat && idf.py -p $port flash"
```

---
> [!TIP]
> 如果烧录失败（端口占用），Agent 会自动查杀占用进程并重试。
