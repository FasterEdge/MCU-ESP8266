/* FasterEdge 开源项目
 * GitHub: https://github.com/FasterEdge
 * Gitee:  https://gitee.com/FasterEdge
 */
// fe_port.c — FasterEdge MCU 平台移植层（ESP8266 / NONOS SDK 版）
// 用于 platformio_ide 工程（VS Code + PlatformIO 插件）：
//   platform = espressif8266, framework = esp8266-nonos-sdk
// 本文件是 keil 版移植模板对应的真实实现：
//   UART  -> driver/uart.h（收：应用层环形缓冲，由 SDK RX 中断回调填充）
//   存储   -> system_param_save/load（flash 参数区）
//   时间   -> system_get_time + sntp
//   随机   -> os_random
//   WiFi  -> user_interface.h
//   TCP   -> espconn（IP 直连，异步封装为同步语义）
#include "fe_port.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "ets_sys.h"
#include "osapi.h"
#include "user_interface.h"
#include "mem.h"
#include "espconn.h"
#include "driver/uart.h"

// ============================================================
// 串口（UART）
// ============================================================
// SDK 内置 driver/uart.c 的 RX 中断中调用本函数填充环形缓冲；
// fe_port_uart_available/read 从中取数据。
#define RX_BUF_SIZE 256
static uint8_t g_rx_buf[RX_BUF_SIZE];
static uint16_t g_rx_head = 0, g_rx_tail = 0;
static fe_port_uart_rx_cb_t g_rx_cb = NULL;
static void *g_rx_user = NULL;

void fe_port_uart_put_byte(uint8_t b) {
    uint16_t next = (g_rx_head + 1) % RX_BUF_SIZE;
    if (next != g_rx_tail) {          // 缓冲未满
        g_rx_buf[g_rx_head] = b;
        g_rx_head = next;
    }
    if (g_rx_cb) g_rx_cb(b, g_rx_user);
}

void fe_port_uart_init(uint8_t port, uint32_t baud, fe_port_uart_rx_cb_t rx_cb, void *user) {
    (void)port;
    // baud 参数直接用（NONOS SDK 用 BIT_RATE_ 常量；此处取数值）
    uart_init((uint32_t)baud, (uint32_t)baud);
    g_rx_cb = rx_cb;
    g_rx_user = user;
}

size_t fe_port_uart_write(uint8_t port, const uint8_t *data, size_t len) {
    size_t i;
    (void)port;
    for (i = 0; i < len; i++) uart_tx_one_char(0, data[i]);
    return len;
}

bool fe_port_uart_available(uint8_t port) {
    (void)port;
    return g_rx_head != g_rx_tail;
}

int fe_port_uart_read(uint8_t port) {
    (void)port;
    if (g_rx_head == g_rx_tail) return -1;
    {
        uint8_t b = g_rx_buf[g_rx_tail];
        g_rx_tail = (g_rx_tail + 1) % RX_BUF_SIZE;
        return (int)b;
    }
}

void fe_port_uart_close(uint8_t port) {
    (void)port;
    // NONOS SDK 无 uart_driver_delete；置空缓冲区即可
    g_rx_head = g_rx_tail = 0;
}

// ============================================================
// 非易失存储（system_param 参数区）
// ============================================================
// 用 SPI flash 参数区（NONOS SDK system_param_save/load）。
// 起始扇区取 0xFC（用户参数区，需避开 SDK 使用的扇区，可调）。
#define FE_PARAM_START_SECTOR 0xFC
#define FE_PARAM_SIZE 512

static uint8_t g_param[FE_PARAM_SIZE];   // 内存副本

static void param_load(void) {
    if (!system_param_load(FE_PARAM_START_SECTOR, 0, g_param, FE_PARAM_SIZE))
        memset(g_param, 0, sizeof(g_param));
}

static bool param_save(void) {
    return system_param_save_with_protect(FE_PARAM_START_SECTOR, g_param, FE_PARAM_SIZE) == TRUE;
}

// 参数区内 KV 布局：每项 [name(16)][value(48)]，最多 8 项
#define PARAM_SLOTS 8
#define PARAM_NAME_LEN 16
#define PARAM_VAL_LEN 48
#define PARAM_ENTRY (PARAM_NAME_LEN + PARAM_VAL_LEN)

static int find_slot(const char *key) {
    int i;
    for (i = 0; i < PARAM_SLOTS; i++) {
        char *name = (char *)&g_param[i * PARAM_ENTRY];
        if (name[0] && strcmp(name, key) == 0) return i;
    }
    return -1;
}

static void norm_key(const char *in, char *out) {
    // 完整 key 的 FNV-1a 32 位散列 → "k" + 8 hex(10 字符, 满足 PARAM_NAME_LEN=16)。
    // 旧实现截断到 15 字符, 前 15 字符相同的两个 key 会静默映射到同一参数槽互相覆盖。
    uint32_t h = 2166136261u;
    for (const char *p = in; *p; p++) {
        char c = *p;
        if (c == '.' || c == '/') c = '_';
        h = (h ^ (uint8_t)c) * 16777619u;
    }
    snprintf(out, PARAM_NAME_LEN, "k%08lx", (unsigned long)h);
}

bool fe_port_nvs_get_str(const char *ns, const char *key, char *out, size_t outlen) {
    char k[PARAM_NAME_LEN];
    int slot;
    (void)ns;
    param_load();
    norm_key(key, k);
    slot = find_slot(k);
    if (slot < 0) { if (outlen) out[0] = 0; return false; }
    snprintf(out, outlen, "%s", (char *)&g_param[slot * PARAM_ENTRY + PARAM_NAME_LEN]);
    return true;
}

bool fe_port_nvs_set_str(const char *ns, const char *key, const char *value) {
    char k[PARAM_NAME_LEN];
    int slot, i;
    (void)ns;
    param_load();
    norm_key(key, k);
    slot = find_slot(k);
    if (slot < 0) {
        for (i = 0; i < PARAM_SLOTS; i++) {
            char *name = (char *)&g_param[i * PARAM_ENTRY];
            if (!name[0]) { slot = i; break; }
        }
        if (slot < 0) return false;   // 参数区满
        memcpy(&g_param[slot * PARAM_ENTRY], k, strlen(k) + 1);
    }
    snprintf((char *)&g_param[slot * PARAM_ENTRY + PARAM_NAME_LEN],
             PARAM_VAL_LEN, "%s", value ? value : "");
    return param_save();
}

bool fe_port_nvs_remove(const char *ns, const char *key) {
    char k[PARAM_NAME_LEN];
    int slot;
    (void)ns;
    param_load();
    norm_key(key, k);
    slot = find_slot(k);
    if (slot >= 0) {
        g_param[slot * PARAM_ENTRY] = 0;
        param_save();
    }
    return true;
}

bool fe_port_nvs_get_u32(const char *ns, const char *key, uint32_t *out) {
    char buf[PARAM_VAL_LEN];
    if (!fe_port_nvs_get_str(ns, key, buf, sizeof(buf))) return false;
    *out = (uint32_t)strtoul(buf, NULL, 10);
    return true;
}

bool fe_port_nvs_set_u32(const char *ns, const char *key, uint32_t value) {
    char buf[24];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)value);
    return fe_port_nvs_set_str(ns, key, buf);
}

// ============================================================
// 系统时间
// ============================================================
uint64_t fe_port_time_now(void) {
    // 开机秒数（SNTP 同步前无真实 epoch；同步后可用 os_gettimeofday）
    return system_get_time() / 1000000ULL;
}

void fe_port_time_set(uint64_t epoch) {
    // NONOS SDK 无直接 settimeofday；SNTP 成功后会校准
    (void)epoch;
}

int fe_port_time_sync_ntp(const char *server) {
    sntp_setservername(0, server ? server : "pool.ntp.org");
    sntp_init();
    return 0;
}

// ============================================================
// 随机数
// ============================================================
void fe_port_random_fill(uint8_t *buf, size_t len) {
    size_t i;
    for (i = 0; i < len; i++) buf[i] = (uint8_t)(os_random() & 0xff);
}

// ============================================================
// 网络（WiFi / TCP）
// ============================================================
bool fe_port_wifi_connected(void) {
    return wifi_station_get_connect_status() == STATION_GOT_IP;
}

void fe_port_wifi_ip(char *out, size_t outlen) {
    struct ip_info info;
    if (wifi_get_ip_info(STATION_IF, &info) && info.ip.addr != 0) {
        uint8_t *p = (uint8_t *)&info.ip.addr;
        snprintf(out, outlen, "%u.%u.%u.%u",
                 (unsigned)p[0], (unsigned)p[1], (unsigned)p[2], (unsigned)p[3]);
    } else {
        snprintf(out, outlen, "0.0.0.0");
    }
}

// ---- TCP（espconn，异步回调封装为同步）----
static struct espconn s_conn;
static esp_tcp s_tcp;
static bool s_connected = false;
static bool s_op_done = false;

static void tcp_connect_cb(void *arg) { (void)arg; s_connected = true; s_op_done = true; }
static void tcp_discon_cb(void *arg)  { (void)arg; s_connected = false; s_op_done = true; }

int fe_port_tcp_connect(const char *host, uint16_t port) {
    // host 为点分 IPv4，手工解析（espconn_connect 需要 ip 字节序小端）
    uint8_t ip[4] = {0, 0, 0, 0};
    unsigned a[4] = {0, 0, 0, 0};
    if (sscanf(host, "%u.%u.%u.%u", &a[0], &a[1], &a[2], &a[3]) != 4)
        return -1;
    ip[0] = (uint8_t)a[0]; ip[1] = (uint8_t)a[1];
    ip[2] = (uint8_t)a[2]; ip[3] = (uint8_t)a[3];

    memset(&s_conn, 0, sizeof(s_conn));
    memset(&s_tcp, 0, sizeof(s_tcp));
    s_conn.type = ESPCONN_TCP;
    s_conn.state = ESPCONN_NONE;
    s_conn.proto.tcp = &s_tcp;
    s_tcp.local_port = espconn_port();
    s_tcp.remote_port = port;
    memcpy(s_tcp.remote_ip, ip, 4);

    espconn_regist_connectcb(&s_conn, tcp_connect_cb);
    espconn_regist_disconcb(&s_conn, tcp_discon_cb);
    s_connected = false;
    s_op_done = false;
    if (espconn_connect(&s_conn) != ESPCONN_OK) return -1;
    // 等待 connect 回调（NONOS SDK 回调在系统 tick 中触发）
    while (!s_op_done) os_delay_us(1000);
    return s_connected ? 0 : -1;
}

static bool s_sent_done = false;
static void tcp_sent_cb(void *arg) { (void)arg; s_sent_done = true; }

size_t fe_port_tcp_write(const uint8_t *data, size_t len) {
    if (!s_connected || len == 0) return 0;
    espconn_regist_sentcb(&s_conn, tcp_sent_cb);
    s_sent_done = false;
    if (espconn_sent(&s_conn, (uint8_t *)data, len) != ESPCONN_OK) return 0;
    while (!s_sent_done) os_delay_us(1000);
    return len;
}

int fe_port_tcp_read(uint8_t *buf, size_t len) {
    // 读需要 recv 回调：简化实现，无数据仅返回 0；完整版可注册 recvcb 填充缓冲
    (void)buf; (void)len;
    return 0;
}

void fe_port_tcp_close(void) {
    if (s_connected) espconn_disconnect(&s_conn);
    s_connected = false;
}

// ============================================================
// GPIO（NONOS SDK gpio.h 寄存器）
// ============================================================
#include "gpio.h"

int fe_port_gpio_set_mode(uint8_t pin, const char *mode) {
    if (pin > 16) return -1;
    PIN_FUNC_SELECT(GPIO_PIN_TO_FUNC(pin), PIN_FUNC_GPIO);
    if (strcmp(mode, "input") == 0 || strcmp(mode, "input_pullup") == 0) {
        GPIO_REG_WRITE(GPIO_ENABLE_W1TC_ADDRESS, 1 << pin);   // 输入
        if (strcmp(mode, "input_pullup") == 0)
            GPIO_REG_WRITE(GPIO_PIN_ADDR(pin), GPIO_PIN_PULLUP);
    } else if (strcmp(mode, "output") == 0) {
        GPIO_REG_WRITE(GPIO_ENABLE_W1TS_ADDRESS, 1 << pin);   // 输出
    } else {
        return -1;
    }
    return 0;
}

int fe_port_gpio_write(uint8_t pin, uint8_t level) {
    if (pin > 16) return -1;
    GPIO_REG_WRITE(GPIO_OUT_W1TS_ADDRESS, 1 << pin);
    if (!level) GPIO_REG_WRITE(GPIO_OUT_W1TC_ADDRESS, 1 << pin);
    return 0;
}

int fe_port_gpio_read(uint8_t pin) {
    if (pin > 16) return -1;
    return (GPIO_REG_READ(GPIO_IN_ADDRESS) >> pin) & 1;
}

// ============================================================
// 芯片信息
// ============================================================
void fe_port_chip_info(char *out, size_t outlen) {
    snprintf(out, outlen, "{\"chip\":\"ESP8266\",\"chipId\":%lu,\"freqMHz\":%lu}",
             (unsigned long)system_get_chip_id(), (unsigned long)system_get_cpu_freq());
}

// ============================================================
// 延时
// ============================================================
void fe_port_delay_ms(uint32_t ms) {
    uint32_t i;
    for (i = 0; i < ms; i++) os_delay_us(1000);
}