#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <zlib.h>
#include <fstream>
#include <sstream>

// --- LZString (header-only, include from src/) ---
#include "lz_string.hpp"

// --- UTF-16 <-> UTF-8 conversion (copied from rpgsave.cpp) ---

static std::string utf16_to_utf8(const lzstring::string& input){
    std::string result;
    result.reserve(input.size() * 2);
    for (size_t i = 0; i < input.size(); ) {
        char16_t ch = input[i];
        if (ch < 0x80) {
            result += static_cast<char>(ch);
            ++i;
        } else if (ch < 0x800) {
            result += static_cast<char>(0xC0 | (ch >> 6));
            result += static_cast<char>(0x80 | (ch & 0x3F));
            ++i;
        } else if (ch >= 0xD800 && ch <= 0xDBFF && i + 1 < input.size() &&
                   input[i+1] >= 0xDC00 && input[i+1] <= 0xDFFF) {
            uint32_t u32Cp = 0x10000 + ((ch - 0xD800) << 10) + (input[i+1] - 0xDC00);
            result += static_cast<char>(0xF0 | (u32Cp >> 18));
            result += static_cast<char>(0x80 | ((u32Cp >> 12) & 0x3F));
            result += static_cast<char>(0x80 | ((u32Cp >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (u32Cp & 0x3F));
            i += 2;
        } else {
            result += static_cast<char>(0xE0 | (ch >> 12));
            result += static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
            result += static_cast<char>(0x80 | (ch & 0x3F));
            ++i;
        }
    }
    return result;
}

static lzstring::string utf8_to_utf16(const std::string& input){
    lzstring::string result;
    result.reserve(input.size());
    for (size_t i = 0; i < input.size(); ) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        uint32_t cp = 0;
        if (c < 0x80) {
            cp = c;
            ++i;
        } else if ((c & 0xE0) == 0xC0) {
            cp = c & 0x1F;
            cp = (cp << 6) | (static_cast<unsigned char>(input[i+1]) & 0x3F);
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            cp = c & 0x0F;
            cp = (cp << 6) | (static_cast<unsigned char>(input[i+1]) & 0x3F);
            cp = (cp << 6) | (static_cast<unsigned char>(input[i+2]) & 0x3F);
            i += 3;
        } else {
            cp = c & 0x07;
            cp = (cp << 6) | (static_cast<unsigned char>(input[i+1]) & 0x3F);
            cp = (cp << 6) | (static_cast<unsigned char>(input[i+2]) & 0x3F);
            cp = (cp << 6) | (static_cast<unsigned char>(input[i+3]) & 0x3F);
            i += 4;
        }
        if (cp <= 0xFFFF) {
            result += static_cast<char16_t>(cp);
        } else {
            cp -= 0x10000;
            result += static_cast<char16_t>(0xD800 | (cp >> 10));
            result += static_cast<char16_t>(0xDC00 | (cp & 0x3FF));
        }
    }
    return result;
}

// --- MV decode/encode ---

static std::string decodeMV(const std::string& raw){
    lzstring::string input;
    input.reserve(raw.size());
    for (char c : raw) {
        input.push_back(static_cast<char16_t>(c));
    }
    lzstring::string decompressed = lzstring::decompressFromBase64(input);
    return utf16_to_utf8(decompressed);
}

static std::string encodeMV(const std::string& jsonStr){
    lzstring::string input = utf8_to_utf16(jsonStr);
    lzstring::string compressed = lzstring::compressToBase64(input);
    std::string result;
    result.reserve(compressed.size());
    for (auto aCh : compressed) {
        result.push_back(static_cast<char>(aCh));
    }
    return result;
}

// --- MZ decode/encode ---

static std::string decodeMZ(const std::string& raw){
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

    z_stream strm{};
    int ret = inflateInit(&strm);
    if (ret != Z_OK) return {};

    strm.next_in = compressed.data();
    strm.avail_in = static_cast<uInt>(compressed.size());

    std::string result;
    unsigned char outBuf[16384];

    do {
        strm.next_out = outBuf;
        strm.avail_out = sizeof(outBuf);
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            inflateEnd(&strm);
            fprintf(stderr, "  MZ inflate failed: %s (ret=%d)\n", strm.msg ? strm.msg : "unknown", ret);
            return {};
        }
        size_t have = sizeof(outBuf) - strm.avail_out;
        result.append(reinterpret_cast<char*>(outBuf), have);
    } while (ret != Z_STREAM_END);

    inflateEnd(&strm);
    result.resize(strm.total_out);
    return result;
}

static std::string encodeMZ(const std::string& jsonStr){
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
            fprintf(stderr, "  MZ deflate failed: %s\n", strm.msg ? strm.msg : "unknown");
            return {};
        }
        size_t have = sizeof(outBuf) - strm.avail_out;
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

// --- Helpers ---

static std::string testDir;

static std::string readFile(const std::string& path){
    std::ifstream f(testDir + "/" + path, std::ios::binary);
    if (!f) { fprintf(stderr, "Cannot open %s/%s\n", testDir.c_str(), path.c_str()); exit(1); }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static bool startsWithBrace(const std::string& s){
    return !s.empty() && s[0] == '{';
}

static const char* C_GREEN = "\033[32m";
static const char* C_RED   = "\033[31m";
static const char* C_BOLD  = "\033[1m";
static const char* C_DIM   = "\033[2m";
static const char* C_RESET = "\033[0m";

static int failures = 0;
static int passes = 0;

static void ok(const char* name, const char* detail){
    printf("  %s[OK]%s   | %s%s%s | %s\n", C_GREEN, C_RESET, C_BOLD, name, C_RESET, detail);
}

static void fail(const char* name, const char* detail){
    printf("  %s[FAIL]%s | %s%s%s | %s\n", C_RED, C_RESET, C_BOLD, name, C_RESET, detail);
}

static void info(const char* line){
    printf("           |           | %s%s%s\n", C_DIM, line, C_RESET);
}

static void check(bool cond, const char* name, const char* detail, const char* extra = nullptr){
    if (cond) {
        ok(name, detail);
        ++passes;
    } else {
        fail(name, detail);
        ++failures;
    }
    if (extra) info(extra);
}

// --- Tests ---

void testMZ(){
    printf("\n%s--- MZ Round-trip: file1.rmmzsave ---%s\n", C_BOLD, C_RESET);
    std::string raw = readFile("file1.rmmzsave");

    std::string json = decodeMZ(raw);

    char buf[256];
    snprintf(buf, sizeof(buf), "raw %zu B -> decompressed %zu B", raw.size(), json.size());
    check(!json.empty() && startsWithBrace(json), "decompress", buf);

    std::string reencoded = encodeMZ(json);
    double ratio = (double)reencoded.size() / (double)raw.size();
    snprintf(buf, sizeof(buf), "ratio %.4f (%zu -> %zu B)", ratio, raw.size(), reencoded.size());
    check(reencoded.size() > 0 && ratio > 0.85 && ratio < 1.15, "re-encode", buf);

    std::string rejson = decodeMZ(reencoded);
    snprintf(buf, sizeof(buf), "decompressed %zu B", rejson.size());
    check(rejson == json, "round-trip", buf, rejson == json ? nullptr : "JSON mismatch after encode->decode");
}

void testMV(){
    printf("\n%s--- MV Round-trip: file2.rpgsave ---%s\n", C_BOLD, C_RESET);
    std::string raw = readFile("file2.rpgsave");

    std::string json = decodeMV(raw);

    char buf[256];
    snprintf(buf, sizeof(buf), "raw %zu B -> decompressed %zu B", raw.size(), json.size());
    check(!json.empty() && startsWithBrace(json), "decompress", buf);

    std::string reencoded = encodeMV(json);
    double ratio = (double)reencoded.size() / (double)raw.size();
    snprintf(buf, sizeof(buf), "ratio %.4f (%zu -> %zu B)", ratio, raw.size(), reencoded.size());
    check(reencoded.size() > 0 && ratio > 0.90 && ratio < 1.10, "re-encode", buf);

    std::string rejson = decodeMV(reencoded);
    snprintf(buf, sizeof(buf), "decompressed %zu B", rejson.size());
    check(rejson == json, "round-trip", buf, rejson == json ? nullptr : "JSON mismatch after encode->decode");
}

int main(int argc, char** argv){
    // Resolve test dir from __FILE__ so tests work from any working directory
    (void)argc; (void)argv;
    testDir = __FILE__;
    auto pos = testDir.rfind('/');
    if (pos != std::string::npos) testDir.resize(pos);
    else testDir = ".";

    testMZ();
    testMV();

    printf("\n%s=== %d passed, %d failed ===%s\n",
        failures ? C_RED : C_GREEN, passes, failures, C_RESET);
    return failures > 0 ? 1 : 0;
}
