#include "rpgsave.h"
#include "lz_string.hpp"
#include "logger.h"

#include <QFile>

static int try_stoi(const std::string& s, int fallback = 0)
{
    try {
        return std::stoi(s);
    } catch (...) {
        return fallback;
    }
}

RPGSave::RPGSave(QObject* parent)
    : QObject(parent), m_activeLocale("en")
{
}

// --- Sparse array helpers ---

std::vector<json> RPGSave::extractSparseArray(const json& obj)
{
    try {
        if (!obj.is_object()) return {};
        if (!obj.contains("_data")) return {};

        const auto& data = obj["_data"];
        if (data.is_array()) {
            return data.get<std::vector<json>>();
        }

        if (data.is_object() && data.contains("@a") && data["@a"].is_array()) {
            return data["@a"].get<std::vector<json>>();
        }
    } catch (...) {
    }
    return {};
}

json RPGSave::buildSparseArray(const std::vector<json>& data)
{
    json result;
    result["@a"] = data;
    result["@c"] = static_cast<int>(data.size());
    return result;
}

// --- JSON sanitization ---

std::string RPGSave::sanitizeJson(const std::string& input)
{
    std::string output;
    output.reserve(input.size() * 2);
    bool inString = false;

    for (size_t i = 0; i < input.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(input[i]);

        if (!inString) {
            output += input[i];
            if (c == '"') inString = true;
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
            inString = false;
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

static std::string utf16_to_utf8(const lzstring::string& input)
{
    std::string result;
    for (size_t i = 0; i < input.size(); ++i) {
        uint32_t cp = static_cast<uint32_t>(input[i]);

        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < input.size()) {
            uint32_t low = static_cast<uint32_t>(input[i + 1]);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            }
        }

        if (cp < 0x80) {
            result += static_cast<char>(cp);
        } else if (cp < 0x800) {
            result += static_cast<char>(0xC0 | (cp >> 6));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            result += static_cast<char>(0xE0 | (cp >> 12));
            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            result += static_cast<char>(0xF0 | (cp >> 18));
            result += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return result;
}

static lzstring::string utf8_to_utf16(const std::string& input)
{
    lzstring::string result;
    for (size_t i = 0; i < input.size(); ) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        uint32_t cp = 0;

        if (c < 0x80) {
            cp = c;
            ++i;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1F;
            if (i + 1 >= input.size()) break;
            cp = (cp << 6) | (input[++i] & 0x3F);
            ++i;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0F;
            if (i + 2 >= input.size()) break;
            cp = (cp << 6) | (input[++i] & 0x3F);
            cp = (cp << 6) | (input[++i] & 0x3F);
            ++i;
        } else if ((c & 0xF8) == 0xF0) {
            cp = c & 0x07;
            if (i + 3 >= input.size()) break;
            cp = (cp << 6) | (input[++i] & 0x3F);
            cp = (cp << 6) | (input[++i] & 0x3F);
            cp = (cp << 6) | (input[++i] & 0x3F);
            ++i;
        } else {
            ++i;
            continue;
        }

        if (cp < 0x10000) {
            result += static_cast<char16_t>(cp);
        } else {
            cp -= 0x10000;
            result += static_cast<char16_t>(0xD800 | (cp >> 10));
            result += static_cast<char16_t>(0xDC00 | (cp & 0x3FF));
        }
    }
    return result;
}

// --- LZString encode/decode (MV/MZ) ---

std::string RPGSave::decodeSaveData(const std::string& raw)
{
    lzstring::string input;
    input.reserve(raw.size());
    for (char c : raw) {
        input.push_back(static_cast<char16_t>(c));
    }

    lzstring::string decompressed = lzstring::decompressFromBase64(input);
    return utf16_to_utf8(decompressed);
}

std::string RPGSave::encodeSaveData(const std::string& jsonStr)
{
    lzstring::string input = utf8_to_utf16(jsonStr);
    lzstring::string compressed = lzstring::compressToBase64(input);

    std::string result;
    result.reserve(compressed.size());
    for (auto ch : compressed) {
        result.push_back(static_cast<char>(ch));
    }
    return result;
}
