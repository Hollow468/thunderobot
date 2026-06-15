# Thunderobot Linux 平台驱动

雷神 (Thunderobot) 笔记本 Linux 平台驱动，支持 GPU 模式切换和 LED 控制。

从雷神 ControlCenter (Windows) 反编译代码逆向而来。

## 支持的设备

- 雷神 (Thunderobot) R16 及同系列笔记本
- 通过 ACPI `\\_SB.GWMI.WSAA` 方法与 EC 固件通信

## 项目结构

```
thunderobot/
├── kernel/                  # 内核模块 (C)
│   ├── thunderobot-core.c   # ACPI WSAA 通信层
│   ├── thunderobot-gpu.c    # GPU 模式切换
│   ├── thunderobot-led.c    # LED 控制
│   ├── Makefile
│   └── dkms.conf
│
├── cli/                     # CLI 工具 (Rust)
│   ├── Cargo.toml
│   └── src/
│       ├── main.rs
│       ├── gpu.rs
│       ├── led.rs
│       └── sysfs.rs
│
└── README.md
```

## 编译

### 内核模块

```bash
cd kernel

# 直接编译
make

# 或使用 DKMS (推荐)
sudo dkms add .
sudo dkms install thunderobot/1.0.0
```

### CLI 工具

```bash
cd cli
cargo build --release
# 二进制文件: target/release/thunderobot
```

## 使用

### 加载内核模块

```bash
sudo insmod thunderobot-core.ko
sudo insmod thunderobot-gpu.ko
sudo insmod thunderobot-led.ko
```

### GPU 模式切换

```bash
# 查看当前 GPU
thunderobot gpu status

# 切换模式
thunderobot gpu set 1    # 混合模式
thunderobot gpu set 2    # 独显模式
thunderobot gpu set 3    # 核显模式
```

### LED 控制

```bash
# 查看 LED 状态
thunderobot led status

# 设置模式
thunderobot led mode 1       # 常亮
thunderobot led mode 3       # 呼吸
thunderobot led mode 6       # 彩虹循环
thunderobot led mode 7       # 氛围灯

# 设置亮度 (0-15)
thunderobot led brightness 15

# 设置颜色 (RRGGBB)
thunderobot led color ff0000    # 红色
thunderobot led color 00ff00    # 绿色
thunderobot led color 0000ff    # 蓝色

# 选择 LED 区域
thunderobot led zone 6          # 全部键盘
thunderobot led zone 8          # Logo

# 应用设置
thunderobot led apply
```

## sysfs 接口

```
/sys/kernel/thunderobot/
├── gpu/
│   └── mode          # RW, GPU 模式 (1/2/3)
└── led/
    ├── mode          # RW, LED 模式 (0-7)
    ├── brightness    # RW, 亮度 (0-15)
    ├── color         # RW, 颜色 (RRGGBB hex)
    ├── zone          # RW, LED 区域 (0, 3-8)
    ├── status        # RO, 当前状态
    └── apply         # WO, 应用设置
```

## LED Zone 编号

| Zone | 名称 | 说明 |
|------|------|------|
| 0 | ALL | 全部 LED |
| 3 | LED3 | 键盘灯区 3 |
| 4 | LED2 | 键盘灯区 2 |
| 5 | LED1 | 键盘灯区 1 |
| 6 | KB_ALL | 全部键盘 LED |
| 7 | TRUNK | 尾灯/灯带 |
| 8 | LOGO | Logo 灯 |

## LED 模式

| 模式 | 值 | 说明 |
|------|-----|------|
| OFF | 0 | 关闭 |
| STATIC | 1 | 常亮 |
| BREATHING | 3 | 呼吸 |
| COLORFUL_CYCLE | 6 | 彩虹循环 |
| AMBILIGHT | 7 | 氛围灯 |

## 卸载

```bash
# 卸载内核模块
sudo rmmod thunderobot-led
sudo rmmod thunderobot-gpu
sudo rmmod thunderobot-core

# 或 DKMS
sudo dkms remove thunderobot/1.0.0 --all
```

## 许可证

GPL-2.0
