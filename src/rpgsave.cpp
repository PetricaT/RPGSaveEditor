#include "rpgsave.h"
#include "lz_string.hpp"
#include "logger.h"

#include <QFile>

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

json RPGSave::buildSparseArray(const std::vector<json>& data){
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

// --- LZString encode/decode (MV/MZ) ---

std::string RPGSave::decodeSaveData(const std::string& raw){
    lzstring::string input;
    input.reserve(raw.size());
    for (char c : raw) {
        input.push_back(static_cast<char16_t>(c));
    }

    lzstring::string decompressed = lzstring::decompressFromBase64(input);
    return utf16_to_utf8(decompressed);
}

std::string RPGSave::encodeSaveData(const std::string& jsonStr){
    lzstring::string input = utf8_to_utf16(jsonStr);
    lzstring::string compressed = lzstring::compressToBase64(input);

    std::string result;
    result.reserve(compressed.size());
    for (auto aCh : compressed) {
        result.push_back(static_cast<char>(aCh));
    }
    return result;
}
