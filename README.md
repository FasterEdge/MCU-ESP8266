<div align="center">
<img src="https://avatars.githubusercontent.com/u/245985800?s=200&v=4" style="width:100px;" width="100"/>
<h2>FasterEdge MCU - ESP8266</h2>
<h3>FasterEdge 框架的 ESP8266 平台实现（Arduino / Keil 双版本）</h3>
</div>

### 一、简介

本项目是 **[FasterEdge](https://github.com/FasterEdge/FasterEdge)** 框架在 **ESP8266** 单片机平台上的实现，将主仓库的 **Ability / Data / Command** 模型移植到资源受限的 MCU 环境。实现的能力子集、命令名、目录结构与 [MCU-ESP32](../MCU-ESP32) 完全一致，仅平台 API 按 ESP8266 适配。

- ✅ **双版本**：`arduino/`（PlatformIO/Arduino C++）与 `keil/`（Keil MDK 裸机 C）
- ✅ 与主仓库**同名同命令**，云边协同对等编程
- ✅ HMAC-SHA256 纯 C 零依赖
- ✅ 平台差异收敛到移植层（`fe_port.h`）

### 二、已实现能力（与 MCU-ESP32 一致）

**Ability（9 个）**：BaseAbility / RoleAbility / TimeAbility / OneKeyAbility / ConfigFileAbility / SerialAbility / MQTTAbility / ModbusAbility / EdgeRoleAbility（命令与主仓库完全一致，详见 [MCU-ESP32 README](../MCU-ESP32/README.md) 第二节）

**Data（4 个）**：BaseData / ConfigData / KeyringData / NetMapData

> 排除项与理由（Cmd/Sh/Bash、Docker/K8s、eKuiper/InfluxDB、TSN 等）同 MCU-ESP32，见其 README 第三节。

### 三、ESP8266 平台适配说明

| 差异点 | ESP32 版 | ESP8266 版 |
|--------|----------|-----------|
| WiFi 库 | `<WiFi.h>` | `<ESP8266WiFi.h>` |
| 芯片 API | `ESP.getEfuseMac()` | `ESP.getChipId()` |
| 随机数 | `esp_random()` | `ESP.random()` |
| 核数 | 双核（`getChipCores()`）| 单核（固定 1）|
| UART 端口 | 0/1/2（`SOC_UART_NUM`）| 0/1（UART1 仅 TX）|
| 板型 | `esp32dev` | `esp12e` / `nodemcuv2` / `d1_mini` |

### 四、目录结构

```
MCU-ESP8266/
├── arduino/                    # PlatformIO / Arduino C++ 版
│   ├── platformio.ini          # ESP8266 构建配置（esp12e）
│   ├── include/                # fe.h / fe_ability.h / fe_data.h / fe_hmac_sha256.h
│   └── src/                    # main.cpp / fe.cpp / register.cpp / ability_*.cpp / data_*.cpp
└── keil/                       # Keil MDK 裸机 C 版
    ├── MDK-ARM/                # FasterEdge-MCU-ESP8266.uvprojx
    ├── Core/                   # fe.h / fe.c / fe_hmac_sha256.c
    ├── Inc/                    # fe_ability.h / fe_data.h / fe_port.h
    ├── Ability/                # ability_*.c（9 个）
    ├── Data/                   # data_*.c（4 个）
    └── User/                   # main.c / register.c / fe_port.c

platformio_ide/                # VS Code + PlatformIO 插件工程（ESP8266 NONOS SDK 框架）
    ├── platformio.ini          # espressif8266 / esp8266-nonos-sdk / esp12e
    ├── .vscode/extensions.json # 推荐 PlatformIO IDE 插件
    ├── include/                # fe.h / fe_ability.h / fe_data.h / fe_port.h / fe_hmac_sha256.h
    └── src/                    # 复用 keil 裸机 C + NONOS SDK 版 fe_port（真实实现）
```

> 三个构建途径对应三个工具链：`arduino/`（Arduino C++）、`keil/`（Keil MDK）、`platformio_ide/`（VS Code PlatformIO 插件 + NONOS SDK），能力与命令完全一致。

### 五、Arduino 版使用

```bash
cd arduino
pio run            # 编译（默认 esp12e 板型）
pio upload         # 烧录
pio device monitor # 串口监视器 (115200)
```

**串口命令示例（与 ESP32 版相同）：**

```
help
ability_BaseAbility list_ability_names
ability_RoleAbility set_role edge
ability_TimeAbility sync_ntp
ability_OneKeyAbility issue_token sensor01
ability_ModbusAbility write_holding 0,42
ability_MQTTAbility set_broker 192.168.1.10:1883
ability_MQTTAbility publish sensors/temp,25.5
data_ConfigData set wifi.ssid=MyNet
data_KeyringData issue_token sensor01
data_NetMapData info
```

> 换板型：编辑 `platformio.ini` 将 `board` 改为 `nodemcuv2` / `d1_mini` / `esp01_1m` 等。

### 六、Keil 版使用

1. 用 Keil MDK 打开 `keil/MDK-ARM/FasterEdge-MCU-ESP8266.uvprojx`
2. 在 `User/fe_port.c` 中按注释完成平台移植（UART / Flash / 时间 / 随机数 / TCP），文件末尾附 **ESP-IDF** 与 **ESP8266 NONOS SDK** 两套参考片段
3. 编译烧录，串口命令格式同上

> 说明：ESP8266 为 Xtensa 内核，Keil MDK 主要面向 ARM Cortex-M。若用 Keil 编译，可将 `fe_port.c` 的 TODO 替换为 NONOS SDK API（末尾附参考），或在其他 Cortex-M MCU 上直接复用本框架。

### 六-b、PlatformIO IDE 版使用（VS Code 插件）

`platformio_ide/` 是 **裸机 C + ESP8266 NONOS SDK 框架** 工程，复用 keil 版 C 代码，`fe_port.c` 为真实 NONOS SDK 实现（UART 环形缓冲 / system_param 存储 / SNTP / os_random / espconn TCP），无需 Keil 即可在 VS Code 中编译烧录。

1. VS Code 安装 **PlatformIO IDE** 插件（打开 `platformio_ide/` 时自动提示）
2. 打开 `platformio_ide/` 目录
3. 底部状态栏点击 **Build** / **Upload** / **Serial Monitor**（115200）

```bash
cd platformio_ide
pio run            # 编译
pio run -t upload  # 烧录
pio device monitor # 串口监视
```

> 与 `arduino/`（Arduino C++ 框架）不同，本版为 ESP8266 NONOS SDK 框架的纯 C 实现；串口命令格式完全一致。完整接收请在 SDK 的 UART 中断中调用 `fe_port_uart_put_byte` 填充缓冲。

### 六-b、MCU 专有模块

除主仓库对应能力外，本仓库提供 3 个 **MCU 专有** 模块（寄存器 / GPIO / 芯片信息），三套代码（arduino / keil / platformio_ide）完全同构，平台差异由 Arduino API 或 `fe_port` 原语隔离：

| 模块 | 类型 | 命令 | 说明 |
|------|------|------|------|
| RegAbility | Ability | `read <addr>` / `write <addr>,<value>` / `bit_set <addr>,<bit>` / `bit_clear <addr>,<bit>` / `info` | 直接读写内存映射外设寄存器（32 位，volatile 指针）|
| GpioAbility | Ability | `mode <pin>,<input\|output\|input_pullup>` / `write <pin>,<0\|1>` / `read <pin>` / `info` | 引脚模式 / 输出 / 读取（GPIO0-16）|
| ChipData | Data | `info` | 芯片 ID / 频率（NONOS `system_get_chip_id`）|

**示例：**

```
ability_RegAbility read 0x60000300
ability_RegAbility write 0x60000300,0x12345678
ability_RegAbility bit_set 0x60000300,7
ability_GpioAbility mode 2,output
ability_GpioAbility write 2,1
ability_GpioAbility read 2
data_ChipData info
```

> ⚠️ 寄存器操作直接访问硬件，误写可能导致系统异常，仅供调试/底层驱动使用。

### 七、与 FasterEdge 主仓库的对应关系

- 命令名与主仓库**完全一致**，与 MCU-ESP32 实现同构
- 令牌/密钥持久化（ESP8266 使用 Arduino 内置 `Preferences`/EEPROM 模拟 NVS）
- 严格类型校验保留

### 八、姊妹项目

- **[FasterEdge MCU - ESP32](https://github.com/FasterEdge/MCU-ESP32)**：双核、更多 UART、BLE 等更强平台
- **[FasterEdge](https://github.com/FasterEdge/FasterEdge)**：框架主仓库
