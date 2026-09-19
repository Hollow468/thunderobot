# Thunderobot Linux 平台驱动与管理工具

雷神 (Thunderobot) 笔记本 Linux 平台驱动与 CLI 管理工具，支持风扇控制、GPU 模式切换、性能模式切换与 RGB LED 灯效调节。

从雷神 ControlCenter (Windows) 反编译代码逆向而来。

---

## 支持的设备

- 雷神 (Thunderobot) R16 / 911 及同系列笔记本（NLXB / NLZD / NLZE / NLY 等平台）
- 通过 ACPI `\\_SB.GWMI.WSAA` 方法与 EC 固件进行 SMI 双向通信

---

## 项目结构

```
thunderobot/
├── kernel/                  # 内核模块 (C)
│   ├── thunderobot.c        # 统一平台驱动（ACPI/GPU/LED/Power/Fan）
│   ├── include/thunderobot.h
│   ├── Makefile
│   └── dkms.conf
│
├── cli/                     # CLI 工具 (Rust)
│   ├── Cargo.toml
│   └── src/
│       ├── main.rs
│       ├── fan.rs
│       ├── gpu.rs
│       ├── led.rs
│       ├── power.rs
│       └── sysfs.rs
│
├── script/                  # 自动化维护脚本
│   └── sync-version.sh      # 版本号全量同步脚本
├── .github/workflows/       # GitHub CI/CD
│   └── release.yml          # 多架构自动化 Release 构建
├── Makefile                 # 项目构建与发布统一编排
└── README.md
```

---

## 编译与安装

### 1. 统一编译 (推荐)

项目根目录提供了 Makefile 编排命令：

```bash
# 编译内核模块与 CLI 工具
make all

# 安装内核模块与 CLI 二进制 (/usr/local/bin/thunderobot)
sudo make install
```

### 2. 内核模块单独编译与 DKMS

```bash
cd kernel

# 直接编译
make

# 或使用 DKMS 管理 (推荐)
sudo dkms add .
sudo dkms install thunderobot/1.3.0
```

加载内核模块：
```bash
sudo modprobe thunderobot  # 如果已通过 dkms / modules_install 安装
# 或直接加载本地编译模块
sudo insmod kernel/thunderobot.ko
```

### 3. CLI 工具单独编译

```bash
cd cli
cargo build --release
# 生成的二进制文件路径: cli/target/release/thunderobot
```

---

## CLI 命令行工具详细使用说明

命令行工具名称为 `thunderobot`，支持层级子命令与 Shell 自动补全。

### 1. 风扇控制 (`thunderobot fan`)

| 子命令 / 参数 | 说明 | 示例 |
|---|---|---|
| `status` | 查看风扇综合状态（实时转速 RPM、硬件温度、目标占空比、控制模式） | `thunderobot fan status` |
| `auto` | 恢复 EC 固件自动温控托管（等同于 `mode 0`） | `thunderobot fan auto` |
| `boost` | 一键满血全速散热（将所有风扇拉满至 100% 占空比） | `thunderobot fan boost` |
| `mode <0\|1>` | 设置风扇控制模式：`0` 为自动/EC托管，`1` 为手动模式 | `thunderobot fan mode 1` |
| `set [SPEED]` | 设置所有风扇的统一占空比 (`0`~`100`% 或 `auto`) | `thunderobot fan set 80` |
| `set --cpu <VAL>` | 单独指定 CPU 风扇占空比 (`0`~`100`%) | `thunderobot fan set --cpu 75` |
| `set --gpu <VAL>` | 单独指定 GPU 风扇占空比 (`0`~`100`%) | `thunderobot fan set --gpu 70` |
| `set --sys <VAL>` | 单独指定 SYS 辅助风扇占空比 (`0`~`100`%，3风扇机型) | `thunderobot fan set --sys 60` |
| `curve show` | 查看机型预设的 8 档温度插值曲线对照表 (30°C ~ 100°C) | `thunderobot fan curve show` |

**使用示例：**
```bash
# 查看实时风扇转速与芯片温度
thunderobot fan status

# 游戏高负载：CPU 85%，GPU 80%
thunderobot fan set --cpu 85 --gpu 80

# 退出游戏，一键恢复 EC 自动温控
thunderobot fan auto
```

---

### 2. GPU 模式切换 (`thunderobot gpu`)

控制笔记本硬件 MUX 开关切换显卡直连/混合模式（切换后重启生效）：

| 模式值 | 模式名称 | 说明 |
|---|---|---|
| `1` | `hybrid` | 混合输出模式 (MSHybrid / Optimus) |
| `2` | `discrete` | 独显直连模式 (dGPU Direct) |
| `3` | `integrated` | 纯核显省电模式 (iGPU Only) |

**使用示例：**
```bash
# 查看当前 GPU 硬件状态
thunderobot gpu status

# 切换为独显直连
thunderobot gpu set 2

# 切换为混合模式
thunderobot gpu set 1
```

---

### 3. 性能模式管理 (`thunderobot power`)

切换 EC 性能释放档位与功耗墙限制：

| 模式值 | 模式名称 | 说明 |
|---|---|---|
| `0` | `High Performance` | 高性能 / 狂暴模式 |
| `1` | `Gaming` | 游戏模式 |
| `2` | `Office / Audio` | 办公 / 静音模式 |

**配置持久化（用户态保存与恢复）：**
```bash
# 查看当前性能模式
thunderobot power status

# 切换为高性能模式
thunderobot power set 0

# 将当前模式保存到本地配置 (~/.config/thunderobot/power.conf)
thunderobot power save

# 开机自启或唤醒后从本地配置恢复
thunderobot power restore
```

---

### 4. RGB LED 灯效控制 (`thunderobot led`)

支持键盘背光、灯带 (Trunk) 与 Logo 灯的独立或全局控制：

#### LED 模式代码 (`mode`)
- `0`: 关闭 (OFF)
- `1`: 常亮 (STATIC)
- `3`: 呼吸 (BREATHING)
- `6`: 彩虹循环 (COLORFUL_CYCLE)
- `7`: 氛围灯 (AMBILIGHT)

#### LED 分区代码 (`zone`)
- `0`: 全部区域 (ALL)
- `3`: 键盘灯区 3
- `4`: 键盘灯区 2
- `5`: 键盘灯区 1
- `6`: 全部键盘灯区 (KB_ALL)
- `7`: 尾灯 / 装饰灯带 (TRUNK)
- `8`: Logo 灯 (LOGO)

**使用示例：**
```bash
# 查看当前 LED 设置与硬件反馈状态
thunderobot led status

# 设置键盘常亮并调节颜色为紫色
thunderobot led zone 6
thunderobot led mode 1
thunderobot led color ff00ff
thunderobot led brightness 15
thunderobot led apply

# 设置尾灯为呼吸模式
thunderobot led zone 7
thunderobot led mode 3
thunderobot led apply
```

---

### 5. Shell 自动补全 (`thunderobot completions`)

生成终端命令行补全脚本：

```bash
# Bash
thunderobot completions bash | sudo tee /etc/bash_completion.d/thunderobot

# Zsh
thunderobot completions zsh > ~/.zfunc/_thunderobot
# 确保 ~/.zshrc 包含: fpath=(~/.zfunc $fpath) && autoload -U compinit && compinit

# Fish
thunderobot completions fish > ~/.config/fish/completions/thunderobot.fish
```

---

## sysfs 接口规范

统一驱动在 Linux sysfs 暴露以下接口供程序或脚本直接交互：

```
/sys/kernel/thunderobot/
├── fan/
│   ├── mode          # RW, 风扇控制模式 (0: 自动EC托管, 1: 手动模式)
│   ├── speed         # RW, 综合风扇转速百分比 (0-100 或 255)
│   ├── cpu_speed     # RW, CPU 风扇目标占空比 (0-100)
│   ├── gpu_speed     # RW, GPU 风扇目标占空比 (0-100)
│   ├── sys_speed     # RW, SYS 风扇目标占空比 (0-100, 3风扇机型)
│   ├── cpu_temp      # RO, CPU 温度 (°C)
│   ├── gpu_temp      # RO, GPU 温度 (°C)
│   ├── sys_temp      # RO, SYS 温度 (°C, 3风扇机型)
│   ├── cpu_rpm       # RO, CPU 风扇转速 (RPM)
│   ├── gpu_rpm       # RO, GPU 风扇转速 (RPM)
│   ├── sys_rpm       # RO, SYS 风扇转速 (RPM, 3风扇机型)
│   ├── fans_count    # RO, 风扇数量 (2 或 3)
│   ├── profile       # RO, EC 默认风扇配置 ID (1..4)
│   └── status        # RO, 综合风扇状态概览
├── gpu/
│   └── mode          # RW, GPU 模式 (1/2/3)
├── power/
│   └── mode          # RW, 性能模式 (0/1/2)
└── led/
    ├── mode          # RW, LED 模式 (0-7)
    ├── brightness    # RW, 亮度 (0-15)
    ├── color         # RW, 颜色 (RRGGBB hex)
    ├── zone          # RW, LED 区域 (0, 3-8)
    ├── status        # RO, 当前状态
    └── apply         # WO, 应用设置
```

---

## 版本发布与维护命令

```bash
# 升级 patch 版本 (x.y.Z -> x.y.Z+1) 并自动全量同步
make version-patch   # 或 make patch

# 升级 minor 版本 (x.Y.0 -> x.Y+1.0) 并自动全量同步
make version-minor   # 或 make minor

# 校验全项目版本号一致性
make check-version
```

---

## 卸载

```bash
# 卸载内核模块
sudo rmmod thunderobot

# 或使用 DKMS 移除
sudo dkms remove thunderobot/1.3.0 --all

# 删除 CLI 二进制
sudo rm -f /usr/local/bin/thunderobot
```

---

## 许可证

GPL-2.0


