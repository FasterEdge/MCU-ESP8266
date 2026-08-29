<div align="center">
<img src="https://avatars.githubusercontent.com/u/245985800?s=200&v=4" style="width:100px;" width="100"/>
<h2>FasterEdge MCU - ESP8266</h2>
<h3>ESP8266 platform implementation of the FasterEdge framework (Arduino / Keil)</h3>
</div>

### 1. Introduction

This project is the **ESP8266** MCU implementation of the **[FasterEdge](https://github.com/FasterEdge/FasterEdge)** framework, porting the **Ability / Data / Command** model to resource-constrained microcontrollers. The implemented capability subset, command names, and directory layout are **identical** to [MCU-ESP32](../MCU-ESP32); only the platform APIs are adapted for ESP8266.

- ✅ **Two versions**: `arduino/` (PlatformIO/Arduino C++) and `keil/` (Keil MDK bare-metal C)
- ✅ **Same names and commands** as the main repo
- ✅ Pure-C HMAC-SHA256, zero dependencies
- ✅ Platform differences isolated in a port layer (`fe_port.h`)

### 2. Implemented Capabilities (same as MCU-ESP32)

**Ability (9)**: BaseAbility / RoleAbility / TimeAbility / OneKeyAbility / ConfigFileAbility / SerialAbility / MQTTAbility / ModbusAbility / EdgeRoleAbility (commands identical to the main repo; see [MCU-ESP32 README](../MCU-ESP32/README_en.md) section 2)

**Data (4)**: BaseData / ConfigData / KeyringData / NetMapData

> Excluded capabilities and rationale are the same as MCU-ESP32 (see its README section 3).

### 3. ESP8266 Platform Adaptation

| Difference | ESP32 version | ESP8266 version |
|------------|---------------|-----------------|
| WiFi library | `<WiFi.h>` | `<ESP8266WiFi.h>` |
| Chip API | `ESP.getEfuseMac()` | `ESP.getChipId()` |
| Random | `esp_random()` | `ESP.random()` |
| Cores | dual (`getChipCores()`) | single (fixed 1) |
| UART ports | 0/1/2 (`SOC_UART_NUM`) | 0/1 (UART1 TX only) |
| Board | `esp32dev` | `esp12e` / `nodemcuv2` / `d1_mini` |

### 4. Directory Layout

```
MCU-ESP8266/
├── arduino/                    # PlatformIO / Arduino C++ version
│   ├── platformio.ini          # ESP8266 build config (esp12e)
│   ├── include/                # fe.h / fe_ability.h / fe_data.h / fe_hmac_sha256.h
│   └── src/                    # main.cpp / fe.cpp / register.cpp / ability_*.cpp / data_*.cpp
└── keil/                       # Keil MDK bare-metal C version
    ├── MDK-ARM/                # FasterEdge-MCU-ESP8266.uvprojx
    ├── Core/                   # fe.h / fe.c / fe_hmac_sha256.c
    ├── Inc/                    # fe_ability.h / fe_data.h / fe_port.h
    ├── Ability/                # ability_*.c (9)
    ├── Data/                   # data_*.c (4)
    └── User/                   # main.c / register.c / fe_port.c

platformio_ide/                # VS Code + PlatformIO plugin project (ESP8266 NONOS SDK framework)
    ├── platformio.ini          # espressif8266 / esp8266-nonos-sdk / esp12e
    ├── .vscode/extensions.json # recommends PlatformIO IDE
    ├── include/                # fe.h / fe_ability.h / fe_data.h / fe_port.h / fe_hmac_sha256.h
    └── src/                    # reuses keil bare-metal C + real NONOS SDK fe_port
```

> Three build routes, three toolchains: `arduino/` (Arduino C++), `keil/` (Keil MDK), `platformio_ide/` (VS Code PlatformIO plugin + NONOS SDK); same commands.

### 5. Arduino Version

```bash
cd arduino
pio run            # build (default esp12e board)
pio upload         # flash
pio device monitor # serial monitor (115200)
```

**Serial command examples (same as ESP32 version):**

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

> To change boards, edit `board` in `platformio.ini` (e.g. `nodemcuv2` / `d1_mini` / `esp01_1m`).

### 6. Keil Version

1. Open `keil/MDK-ARM/FasterEdge-MCU-ESP8266.uvprojx` with Keil MDK
2. Implement the platform port in `User/fe_port.c` (UART / Flash / time / random / TCP); both **ESP-IDF** and **ESP8266 NONOS SDK** reference snippets are included at the end of the file
3. Build, flash, and use the same serial command format

> Note: ESP8266 uses the Xtensa core while Keil MDK mainly targets ARM Cortex-M. For Keil builds, replace the TODOs in `fe_port.c` with NONOS SDK APIs, or reuse this framework on other Cortex-M MCUs.

### 7. PlatformIO IDE Version (VS Code plugin)

`platformio_ide/` is a **bare-metal C + ESP8266 NONOS SDK framework** project that reuses the keil C code with a real NONOS SDK `fe_port.c` (UART ring buffer / system_param storage / SNTP / os_random / espconn TCP). No Keil needed — build and flash right from VS Code.

1. Install the **PlatformIO IDE** extension in VS Code (prompted when opening `platformio_ide/`)
2. Open the `platformio_ide/` directory
3. Click **Build** / **Upload** / **Serial Monitor** (115200) in the status bar

```bash
cd platformio_ide
pio run            # build
pio run -t upload  # flash
pio device monitor # serial monitor
```

> Unlike `arduino/` (Arduino C++ framework), this version is a pure-C implementation on the NONOS SDK framework; serial commands are identical. For full reception, call `fe_port_uart_put_byte` from the SDK UART interrupt to fill the ring buffer.

### 8. MCU-Specific Modules

Beyond the main-repo capabilities, this repo adds 3 **MCU-specific** modules (registers / GPIO / chip info), identical across all three builds; platform differences are isolated behind the Arduino API or `fe_port` primitives:

| Module | Type | Commands | Description |
|--------|------|----------|-------------|
| RegAbility | Ability | `read <addr>` / `write <addr>,<value>` / `bit_set <addr>,<bit>` / `bit_clear <addr>,<bit>` / `info` | Direct read/write of memory-mapped peripheral registers (32-bit, volatile pointer) |
| GpioAbility | Ability | `mode <pin>,<input|output|input_pullup>` / `write <pin>,<0|1>` / `read <pin>` / `info` | Pin mode / output / input (GPIO0-16) |
| ChipData | Data | `info` | Chip ID / freq (NONOS `system_get_chip_id`) |

**Examples:**

```
ability_RegAbility read 0x60000300
ability_RegAbility write 0x60000300,0x12345678
ability_RegAbility bit_set 0x60000300,7
ability_GpioAbility mode 2,output
ability_GpioAbility write 2,1
ability_GpioAbility read 2
data_ChipData info
```

> ⚠️ Register access touches hardware directly; a wrong write may crash the system. Debug/low-level use only.

### 9. Correspondence with the Main Repo

- Command names are **identical** to the main repo, structurally identical to MCU-ESP32
- Tokens/secrets persisted (ESP8266 uses Arduino built-in `Preferences`/EEPROM as NVS)
- Strict type checking retained

### 10. Sister Projects

- **[FasterEdge MCU - ESP32](https://github.com/FasterEdge/MCU-ESP32)**: dual-core, more UARTs, BLE
- **[FasterEdge](https://github.com/FasterEdge/FasterEdge)**: main framework repo
