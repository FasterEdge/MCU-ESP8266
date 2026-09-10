// FasterEdge 开源项目 - Github: https://github.com/FasterEdge - Gitee: https://gitee.com/FasterEdge
//
// fe_prefs_esp8266.h — ESP8266 的 Preferences 兼容封装(EEPROM 实现)。
// ESP8266 的 Arduino 框架没有 NVS/Preferences 库(那是 ESP32 的特性);
// 本封装用 EEPROM(闪存模拟)实现本仓库用到的 Preferences 子集:
//   begin/end, getString/putString, getUInt/putUInt, getBool/putBool,
//   isKey, remove
// 语义对齐 ESP32 Preferences: 键按 "namespace" 隔离(仅 ESP8266 单 EEPROM 全局共享,
// 因此用 ns 前缀区分); putXxx 失败返回 0, 调用处据此报错。
#ifndef FE_PREFS_ESP8266_H
#define FE_PREFS_ESP8266_H

#if defined(ESP8266)

#include <Arduino.h>
#include <EEPROM.h>
#include <string.h>
#include <stdlib.h>
#include <vector>
#include <utility>

class Preferences {
public:
    Preferences() : _open(false), _dirty(false) {}

    // 打开命名空间; ESP8266 单 EEPROM, 用前缀隔离命名空间。
    bool begin(const char *ns, bool readOnly = false) {
        (void)readOnly;             // EEPROM 不做写保护, 只读标记仅作文档
        _ns = ns ? ns : "";
        if (!_open) {
            EEPROM.begin(kSize);    // ESP8266 EEPROM.begin 返回 void
            _load();
            _open = true;
        }
        _dirty = false;
        return true;
    }

    void end() {
        if (_open) {
            if (_dirty) {
                _commit();                  // 先序列化写 EEPROM 缓冲区
                EEPROM.commit();            // 再真正持久化(commit 内部也会写回脏页)
            }
            _open = false;
            _kv.clear();
        }
    }

    String getString(const char *key, const String &def) const {
        size_t i;
        if (_find(key, i)) return _kv[i].val;
        return def;
    }

    size_t putString(const char *key, const String &val) {
        if (!_set(key, val)) return 0;      // 容量不足/失败 -> 0
        return val.length();
    }

    uint32_t getUInt(const char *key, uint32_t def) const {
        size_t i;
        if (_find(key, i)) return (uint32_t)strtoul(_kv[i].val.c_str(), NULL, 10);
        return def;
    }

    size_t putUInt(const char *key, uint32_t val) {
        char buf[12];
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)val);
        if (!_set(key, String(buf))) return 0;
        return sizeof(uint32_t);
    }

    bool getBool(const char *key, bool def) const {
        size_t i;
        if (_find(key, i)) return _kv[i].val == "1";
        return def;
    }

    size_t putBool(const char *key, bool val) {
        if (!_set(key, val ? String("1") : String("0"))) return 0;
        return 1;
    }

    bool isKey(const char *key) const {
        size_t i;
        return _find(key, i);
    }

    bool remove(const char *key) {
        size_t i;
        if (!_find(key, i)) return false;   // 不存在 -> false(与 ESP32 一致)
        _kv.erase(_kv.begin() + i);
        _dirty = true;
        return true;
    }

private:
    static const size_t kSize = 4096;       // EEPROM 空间(4KB)
    struct Entry {
        String ns;
        String key;
        String val;
    };
    std::vector<Entry> _kv;
    String _ns;
    bool _open;
    bool _dirty;

    static String _fullKey(const String &ns, const String &key) {
        if (ns.length() == 0) return key;
        String f;
        f.reserve(ns.length() + 1 + key.length());
        f = ns;
        f += '\x1f';                        // 分隔符(键内不会出现)
        f += key;
        return f;
    }

    // 返回 kv 列表中全键 _ns+sep+key 的位置。
    bool _find(const char *key, size_t &idx) const {
        String want = _fullKey(_ns, key);
        for (size_t i = 0; i < _kv.size(); i++) {
            if (_kv[i].ns == _ns && _kv[i].key == String(key)) { idx = i; return true; }
        }
        (void)want;
        return false;
    }

    // 序列化后占用字节数(不含头部 4 字节 magic)。
    static size_t _serializedSize(const std::vector<Entry> &kv) {
        size_t n = 0;
        for (size_t i = 0; i < kv.size(); i++) {
            n += 1 + kv[i].ns.length();     // nsLen + ns
            n += 1 + kv[i].key.length();    // keyLen + key
            n += 2 + kv[i].val.length();    // valLen + val
        }
        n += 1;                             // 结束符 0x00
        return n;
    }

    // 设置键值; 容量不足(序列化超 EEPROM)时返回 false 且不改动。
    bool _set(const String &key, const String &val) {
        size_t idx;
        std::vector<Entry> tmp = _kv;
        if (_find(key.c_str(), idx)) {
            tmp[idx].val = val;
        } else {
            Entry e;
            e.ns = _ns;
            e.key = key;
            e.val = val;
            tmp.push_back(e);
        }
        if (_serializedSize(tmp) + 4 > kSize) return false;
        _kv.swap(tmp);
        _dirty = true;
        return true;
    }

    // 从 EEPROM 全量载入 kv 列表。
    void _load() {
        _kv.clear();
        size_t pos = 4;                     // 跳过 magic
        bool magicOk = (EEPROM.read(0) == 'F' && EEPROM.read(1) == 'E'
                        && EEPROM.read(2) == 'P' && EEPROM.read(3) == '1');
        if (!magicOk) return;               // 未初始化: 视为空
        while (pos < kSize) {
            uint8_t nl = EEPROM.read(pos);
            if (nl == 0) break;             // 结束符
            if (pos + 1 + nl > kSize) break;
            String ns;
            for (uint8_t i = 0; i < nl; i++) ns += (char)EEPROM.read(pos + 1 + i);
            pos += 1 + nl;
            if (pos >= kSize) break;
            uint8_t kl = EEPROM.read(pos);
            if (kl == 0) break;
            if (pos + 1 + kl > kSize) break;
            String key;
            for (uint8_t i = 0; i < kl; i++) key += (char)EEPROM.read(pos + 1 + i);
            pos += 1 + kl;
            if (pos + 2 > kSize) break;
            uint16_t vl = EEPROM.read(pos) | (EEPROM.read(pos + 1) << 8);
            pos += 2;
            if (pos + vl > kSize) break;
            String val;
            for (uint16_t i = 0; i < vl; i++) val += (char)EEPROM.read(pos + i);
            pos += vl;
            Entry e;
            e.ns = ns;
            e.key = key;
            e.val = val;
            _kv.push_back(e);
            if (pos >= kSize) break;
        }
    }

    void _commit() {
        // 写 magic
        EEPROM.write(0, 'F');
        EEPROM.write(1, 'E');
        EEPROM.write(2, 'P');
        EEPROM.write(3, '1');
        size_t pos = 4;
        for (size_t i = 0; i < _kv.size(); i++) {
            EEPROM.write(pos, (uint8_t)_kv[i].ns.length());
            for (size_t j = 0; j < _kv[i].ns.length(); j++) EEPROM.write(pos + 1 + j, (uint8_t)_kv[i].ns[j]);
            pos += 1 + _kv[i].ns.length();
            EEPROM.write(pos, (uint8_t)_kv[i].key.length());
            for (size_t j = 0; j < _kv[i].key.length(); j++) EEPROM.write(pos + 1 + j, (uint8_t)_kv[i].key[j]);
            pos += 1 + _kv[i].key.length();
            EEPROM.write(pos, (uint8_t)(_kv[i].val.length() & 0xFF));
            EEPROM.write(pos + 1, (uint8_t)((_kv[i].val.length() >> 8) & 0xFF));
            pos += 2;
            for (size_t j = 0; j < _kv[i].val.length(); j++) EEPROM.write(pos + j, (uint8_t)_kv[i].val[j]);
            pos += _kv[i].val.length();
        }
        EEPROM.write(pos, 0);               // 结束符
    }
};

#endif // ESP8266
#endif // FE_PREFS_ESP8266_H