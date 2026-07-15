#include "rpgsave.h"
#include "lz_string.hpp"
#include "logger.h"

#include <QFile>
#include <zlib.h>

static int try_stoi(const std::string& s, int iFallback = 0){
    try {
        return std::stoi(s);
    } catch (...) {
        return iFallback;
    }
}

RPGSave::RPGSave(QObject* parent)
    : QObject(parent), m_activeLocale("en"){
}

void RPGSave::reset(){
    m_bLoaded = false;
    m_bMZFormat = false;
    m_filePath.clear();
    m_error.clear();
    m_root = json();
    m_nativeVariableNames.clear();
    m_nativeSwitchNames.clear();
    m_locales.clear();
    m_activeLocale = "en";
    m_variableNames.clear();
    m_switchNames.clear();
    m_iMaxItemId = 0;
    m_iMaxWeaponId = 0;
    m_iMaxArmorId = 0;
    m_iMaxActorId = 0;
    m_itemNames.clear();
    m_weaponNames.clear();
    m_armorNames.clear();
    m_actorNames.clear();
}

// --- Sparse array helpers ---

std::vector<json> RPGSave::extractSparseArray(const json& obj){
    try {
        if (!obj.is_object()) return {};
        if (!obj.contains("_data")) return {};

        const auto& aData = obj["_data"];
        if (aData.is_array()) {
            return aData.get<std::vector<json>>();
        }

        if (aData.is_object() && aData.contains("@a") && aData["@a"].is_array()) {
            return aData["@a"].get<std::vector<json>>();
        }
    } catch (...) {
    }
    return {};
}

json RPGSave::buildSparseArray(const std::vector<json>& data) const{
    if (m_bMZFormat) {
        return data;
    }
    json result;
    result["@a"] = data;
    result["@c"] = static_cast<int>(data.size());
    return result;
}

// --- JSON sanitization ---

std::string RPGSave::sanitizeJson(const std::string& input){
    std::string output;
    output.reserve(input.size() * 2);
    bool bInString = false;

    for (size_t i = 0; i < input.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(input[i]);

        if (!bInString) {
            output += input[i];
            if (c == '"') bInString = true;
            continue;
        }

        if (c == '\\' && i + 1 < input.size()) {
            output += input[i];
            ++i;
            output += input[i];
            unsigned char next = static_cast<unsigned char>(input[i]);
            if (next == 'u' || next == 'U') {
                for (int j = 0; j < 4 && i + 1 < input.size(); ++j) {
                    ++i;
                    output += input[i];
                }
            }
            continue;
        }

        if (c == '"') {
            bInString = false;
            output += input[i];
            continue;
        }

        if (c < 0x20) {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", c);
            output += buf;
        } else {
            output += input[i];
        }
    }

    return output;
}

// --- LZString UTF-16 <-> UTF-8 conversion ---

static std::string utf16_to_utf8(const lzstring::string& input){
    std::string result;
    for (size_t i = 0; i < input.size(); ++i) {
        uint32_t u32Cp = static_cast<uint32_t>(input[i]);

        if (u32Cp >= 0xD800 && u32Cp <= 0xDBFF && i + 1 < input.size()) {
            uint32_t u32Low = static_cast<uint32_t>(input[i + 1]);
            if (u32Low >= 0xDC00 && u32Low <= 0xDFFF) {
                u32Cp = 0x10000 + ((u32Cp - 0xD800) << 10) + (u32Low - 0xDC00);
                ++i;
            }
        }

        if (u32Cp < 0x80) {
            result += static_cast<char>(u32Cp);
        } else if (u32Cp < 0x800) {
            result += static_cast<char>(0xC0 | (u32Cp >> 6));
            result += static_cast<char>(0x80 | (u32Cp & 0x3F));
        } else if (u32Cp < 0x10000) {
            result += static_cast<char>(0xE0 | (u32Cp >> 12));
            result += static_cast<char>(0x80 | ((u32Cp >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (u32Cp & 0x3F));
        } else {
            result += static_cast<char>(0xF0 | (u32Cp >> 18));
            result += static_cast<char>(0x80 | ((u32Cp >> 12) & 0x3F));
            result += static_cast<char>(0x80 | ((u32Cp >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (u32Cp & 0x3F));
        }
    }
    return result;
}

static lzstring::string utf8_to_utf16(const std::string& input){
    lzstring::string result;
    for (size_t i = 0; i < input.size(); ) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        uint32_t u32Cp = 0;

        if (c < 0x80) {
            u32Cp = c;
            ++i;
        } else if ((c & 0xE0) == 0xC0) {
            u32Cp = c & 0x1F;
            if (i + 1 >= input.size()) break;
            u32Cp = (u32Cp << 6) | (input[++i] & 0x3F);
            ++i;
        } else if ((c & 0xF0) == 0xE0) {
            u32Cp = c & 0x0F;
            if (i + 2 >= input.size()) break;
            u32Cp = (u32Cp << 6) | (input[++i] & 0x3F);
            u32Cp = (u32Cp << 6) | (input[++i] & 0x3F);
            ++i;
        } else if ((c & 0xF8) == 0xF0) {
            u32Cp = c & 0x07;
            if (i + 3 >= input.size()) break;
            u32Cp = (u32Cp << 6) | (input[++i] & 0x3F);
            u32Cp = (u32Cp << 6) | (input[++i] & 0x3F);
            u32Cp = (u32Cp << 6) | (input[++i] & 0x3F);
            ++i;
        } else {
            ++i;
            continue;
        }

        if (u32Cp < 0x10000) {
            result += static_cast<char16_t>(u32Cp);
        } else {
            u32Cp -= 0x10000;
            result += static_cast<char16_t>(0xD800 | (u32Cp >> 10));
            result += static_cast<char16_t>(0xDC00 | (u32Cp & 0x3FF));
        }
    }
    return result;
}

// --- LZString encode/decode (MV) ---

std::string RPGSave::decodeSaveData_MV(const std::string& raw){
    lzstring::string input;
    input.reserve(raw.size());
    for (char c : raw) {
        input.push_back(static_cast<char16_t>(c));
    }

    lzstring::string decompressed = lzstring::decompressFromBase64(input);
    return utf16_to_utf8(decompressed);
}

std::string RPGSave::encodeSaveData_MV(const std::string& jsonStr){
    lzstring::string input = utf8_to_utf16(jsonStr);
    lzstring::string compressed = lzstring::compressToBase64(input);

    std::string result;
    result.reserve(compressed.size());
    for (auto aCh : compressed) {
        result.push_back(static_cast<char>(aCh));
    }
    return result;
}

// --- Pako/zlib encode/decode (MZ) ---

std::string RPGSave::decodeSaveData_MZ(const std::string& raw){
    // RPG Maker MZ writes pako.deflate output as a string where each char is a
    // byte value (0-255). fs.writeFileSync encodes this as UTF-8, so bytes >= 128
    // become multi-byte UTF-8 sequences. To recover the original deflate stream,
    // we decode UTF-8 and extract each code point as a byte.
    std::vector<unsigned char> compressed;
    compressed.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ) {
        unsigned char c = static_cast<unsigned char>(raw[i]);
        uint32_t cp = 0;
        if (c < 0x80) {
            cp = c;
            ++i;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1F;
            if (i + 1 >= raw.size()) break;
            cp = (cp << 6) | (static_cast<unsigned char>(raw[i + 1]) & 0x3F);
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0F;
            if (i + 2 >= raw.size()) break;
            cp = (cp << 6) | (static_cast<unsigned char>(raw[i + 1]) & 0x3F);
            cp = (cp << 6) | (static_cast<unsigned char>(raw[i + 2]) & 0x3F);
            i += 3;
        } else if ((c & 0xF8) == 0xF0) {
            cp = c & 0x07;
            if (i + 3 >= raw.size()) break;
            cp = (cp << 6) | (static_cast<unsigned char>(raw[i + 1]) & 0x3F);
            cp = (cp << 6) | (static_cast<unsigned char>(raw[i + 2]) & 0x3F);
            cp = (cp << 6) | (static_cast<unsigned char>(raw[i + 3]) & 0x3F);
            i += 4;
        } else {
            ++i;
            continue;
        }
        compressed.push_back(static_cast<unsigned char>(cp & 0xFF));
    }

    // pako.deflate produces zlib format (windowBits=15 by default)
    z_stream strm{};
    int ret = inflateInit(&strm);
    if (ret != Z_OK) return {};

    strm.next_in = compressed.data();
    strm.avail_in = static_cast<uInt>(compressed.size());

    std::string result;
    result.reserve(compressed.size() * 4);
    unsigned char outBuf[16384];

    do {
        strm.next_out = outBuf;
        strm.avail_out = sizeof(outBuf);
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            inflateEnd(&strm);
            LOG_ERROR("MZ zlib inflate failed: {} (ret={})", strm.msg ? strm.msg : "unknown", ret);
            return {};
        }
        size_t have = sizeof(outBuf) - strm.avail_out;
        result.append(reinterpret_cast<char*>(outBuf), have);
    } while (ret != Z_STREAM_END);

    inflateEnd(&strm);
    result.resize(strm.total_out);
    return result;
}

std::string RPGSave::encodeSaveData_MZ(const std::string& jsonStr){
    // pako.deflate(json, {to: "string", level: 1}) uses zlib format with level 1
    z_stream strm{};
    int ret = deflateInit(&strm, 1);
    if (ret != Z_OK) return {};

    strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(jsonStr.data()));
    strm.avail_in = static_cast<uInt>(jsonStr.size());

    std::string result;
    unsigned char outBuf[16384];

    do {
        strm.next_out = outBuf;
        strm.avail_out = sizeof(outBuf);
        ret = deflate(&strm, Z_FINISH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            deflateEnd(&strm);
            LOG_ERROR("MZ zlib deflate failed: {}", strm.msg ? strm.msg : "unknown");
            return {};
        }
        size_t have = sizeof(outBuf) - strm.avail_out;
        // Each output byte becomes a character with that code point (0-255).
        // We write it as UTF-8 to match pako's {to:"string"} + fs.writeFileSync behavior.
        for (size_t i = 0; i < have; ++i) {
            unsigned char b = outBuf[i];
            if (b < 0x80) {
                result += static_cast<char>(b);
            } else {
                result += static_cast<char>(0xC0 | (b >> 6));
                result += static_cast<char>(0x80 | (b & 0x3F));
            }
        }
    } while (ret != Z_STREAM_END);

    deflateEnd(&strm);
    return result;
}
