# Garden UI Sim — PC LVGL 仿真器

在 PC 上预览 AI 桌面能量花园的 UI 效果，无需硬件。

## 环境要求

- Windows 10/11
- MSYS2（UCRT64 或 MINGW64 环境）
- SDL2

## 安装步骤

### 1. 安装 MSYS2

从 https://www.msys2.org 下载安装，默认路径 `C:\msys64`。

### 2. 打开 MSYS2 UCRT64 终端

开始菜单搜索 "MSYS2 UCRT64"，打开蓝色图标终端。

### 3. 配置国内镜像（可选，加速下载）

```bash
sed -i 's|https://mirror.msys2.org/|https://mirrors.tuna.tsinghua.edu.cn/msys2/|g' /etc/pacman.d/mirrorlist.*
pacman -Sy
```

### 4. 安装依赖

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-make mingw-w64-ucrt-x86_64-SDL2 git
```

### 5. 获取 LVGL 源码

```bash
cd /e/github/ai-desktop-energy--garden/garden-ui-sim
git clone --depth=1 --branch v9.5.0 https://github.com/lvgl/lvgl.git
```

如果 GitHub 慢，也可以手动下载 zip 解压到 `garden-ui-sim/lvgl/` 目录。

> 注意：需要删除 ARM 专用汇编目录，否则 x86 编译报错：
> ```bash
> rm -rf lvgl/src/draw/sw/blend/helium
> ```

### 6. 构建

```bash
cd /e/github/ai-desktop-energy--garden/garden-ui-sim
cmake -B build -G "MinGW Makefiles" -DCMAKE_MAKE_PROGRAM=$(which mingw32-make)
cmake --build build -j4
```

### 7. 运行

```bash
./build/garden-sim.exe
```

弹出 1280×452 窗口，显示像素风花园场景。

## 操作说明

| 操作 | 效果 |
|------|------|
| 鼠标点击花园区域 | 浇水（能量 +10，粒子爆发） |
| 点击 WATER 按钮 | 同上 |
| Enter 键 | 短按 = 浇水 |
| Enter 长按 (>500ms) | 长按事件 |
| Enter 双击 | 双击事件 |
| ← → 方向键 | 编码器模拟（预留页面切换） |
| 数字键 1-6 | 切换植物生长阶段 |
| ESC | 退出 |

## 植物生长阶段

| 键 | 阶段 | 外观 |
|----|------|------|
| 1 | 种子 | 土里的小棕色点 |
| 2 | 发芽 | 小绿芽冒出 |
| 3 | 幼苗 | 小茎 + 两片叶子 |
| 4 | 成株 | 完整茎叶 + 花苞 |
| 5 | 开花 | 大花朵盛开 |
| 6 | 散种 | 蒲公英绒球 + 种子飘散 |

## 目录结构

```
garden-ui-sim/
├── CMakeLists.txt      # 构建配置
├── lv_conf.h           # LVGL 配置
├── main.c              # PC 平台层（SDL2 + 输入映射）
├── garden_ui.c         # 可移植 UI 层（纯 LVGL API）
├── garden_ui.h         # 公开 API
├── lvgl/               # LVGL 9.5.0 源码（需手动获取）
└── build/              # 构建产物（git 忽略）
```

## 移植到 ESP32-P4

详见 [ESP32-P4 移植指南](../docs/garden-ui-sim-porting-guide.md)。

简要步骤：复制 `garden_ui.c` 和 `garden_ui.h` 到 ESP32-P4 工程，将 `fill_rect` 直写像素替换为 LVGL layer API（PPA 硬件加速），在 `app_main.c` 中调用 `garden_ui_init()` 和 `garden_ui_tick()`。

## 性能说明

PC 仿真器使用"静态背景缓存"策略：
- 启动时将所有静态元素（天空、山丘、栅栏、小花等）绘制一次，缓存到 `s_bg_buf`（~800KB）
- 每帧 memcpy 背景 + 只绘制动态元素（云、植物、蝴蝶、鸟、粒子、水滴）
- 这是 PC 上的 workaround，ESP32-P4 上应使用 LVGL 原生脏矩形刷新
