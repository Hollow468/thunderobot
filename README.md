# Thunderobot Linux 平台驱动

雷神 (Thunderobot) 笔记本 Linux 平台驱动，支持 GPU 模式切换、LED 控制、性能模式切换与风扇控制。

从雷神 ControlCenter (Windows) 反编译代码逆向而来。

## 支持的设备

- 雷神 (Thunderobot) R16 及同系列笔记本
- 通过 ACPI `\\_SB.GWMI.WSAA` 方法与 EC 固件通信

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
sudo dkms install thunderobot/1.3.0
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
sudo insmod thunderobot.ko
```

### 风扇控制

```bash
# 查看风扇状态（实时转速、温度、占空比、控制模式）
thunderobot fan status

# 恢复 EC 自动温控
thunderobot fan auto
# 或
thunderobot fan mode 0

# 切换为手动控制
thunderobot fan mode 1

# 设置风扇转速 (0-100%)
thunderobot fan set 80                    # 所有风扇 80%
thunderobot fan set --cpu 80 --gpu 70     # 单独指定 CPU / GPU 转速
thunderobot fan set auto                  # 恢复自动

# 一键满血全速 (100% 狂暴散热)
thunderobot fan boost

# 查看机型预设的 8 档温度插值曲线
thunderobot fan curve show
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

### 性能模式切换

```bash
# 查看当前性能模式
thunderobot power status

# 切换模式
thunderobot power set 0    # 高性能模式
thunderobot power set 1    # 游戏模式
thunderobot power set 2    # 办公/音频模式

# 保存当前性能模式到本地配置
thunderobot power save

# 从本地配置恢复性能模式
thunderobot power restore
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
├── fan/
│   ├── mode          # RW, 风扇控制模式 (0: 自动EC托管, 1: 手动模式)
│   ├── speed         # RW, 综合风扇转速百分比 (0-100 或 255)
│   ├── cpu_speed     # RW, CPU 风扇目标占空比 (0-100)
│   ├── gpu_speed     # RW, GPU 风扇目标占空比 (0-100)
│   ├── sys_speed     # RW, SYS 风扇目标占空比 (0-100)
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

## 性能模式

| 模式 | 值 | 说明 |
|------|-----|------|
| HIGH_PERFORMANCE | 0 | 高性能 |
| GAMING | 1 | 游戏模式 |
| OFFICE_AUDIO | 2 | 办公/音频模式 |

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
sudo rmmod thunderobot

# 或 DKMS
sudo dkms remove thunderobot/1.3.0 --all
```

## 许可证

GPL-2.0

