#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <zlib.h>
#include <nlohmann/json.hpp>

using json = nlohmann::ordered_json;

static std::string testDir;

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
    if (cond) { ok(name, detail); ++passes; }
    else { fail(name, detail); ++failures; }
    if (extra) info(extra);
}

static std::string readFile(const std::string& path){
    std::ifstream f(testDir + "/" + path, std::ios::binary);
    if (!f) { fprintf(stderr, "Cannot open %s/%s\n", testDir.c_str(), path.c_str()); exit(1); }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::string decodeMZ(const std::string& raw){
    std::vector<unsigned char> compressed;
    compressed.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ) {
        unsigned char c = static_cast<unsigned char>(raw[i]);
        uint32_t cp = 0;
        if (c < 0x80) { cp = c; ++i; }
        else if ((c & 0xE0) == 0xC0) {
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
        } else { ++i; continue; }
        compressed.push_back(static_cast<unsigned char>(cp & 0xFF));
    }
    z_stream strm{};
    inflateInit(&strm);
    strm.next_in = compressed.data();
    strm.avail_in = static_cast<uInt>(compressed.size());
    std::string result;
    unsigned char buf[16384];
    int ret;
    do {
        strm.next_out = buf;
        strm.avail_out = sizeof(buf);
        ret = inflate(&strm, Z_NO_FLUSH);
        result.append(reinterpret_cast<char*>(buf), sizeof(buf) - strm.avail_out);
    } while (ret != Z_STREAM_END);
    inflateEnd(&strm);
    result.resize(strm.total_out);
    return result;
}

static std::string encodeMZ(const std::string& jsonStr){
    z_stream strm{};
    deflateInit(&strm, 1);
    strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(jsonStr.data()));
    strm.avail_in = static_cast<uInt>(jsonStr.size());
    std::string result;
    unsigned char buf[16384];
    int ret;
    do {
        strm.next_out = buf;
        strm.avail_out = sizeof(buf);
        ret = deflate(&strm, Z_FINISH);
        size_t have = sizeof(buf) - strm.avail_out;
        for (size_t i = 0; i < have; ++i) {
            unsigned char b = buf[i];
            if (b < 0x80) result += static_cast<char>(b);
            else {
                result += static_cast<char>(0xC0 | (b >> 6));
                result += static_cast<char>(0x80 | (b & 0x3F));
            }
        }
    } while (ret != Z_STREAM_END);
    deflateEnd(&strm);
    return result;
}

int main(){
    testDir = __FILE__;
    auto pos = testDir.rfind('/');
    if (pos != std::string::npos) testDir.resize(pos);
    else testDir = ".";

    printf("\n%s--- JSON Compat: file1.rmmzsave ---%s\n", C_BOLD, C_RESET);

    std::string raw = readFile("file1.rmmzsave");
    std::string originalJson = decodeMZ(raw);
    json root = json::parse(originalJson);
    std::string reserializedJson = root.dump();

    char buf[256];
    bool jsonIdentical = (originalJson == reserializedJson);
    snprintf(buf, sizeof(buf), "%zu B -> parse -> dump = %zu B", originalJson.size(), reserializedJson.size());
    check(jsonIdentical, "parse+dump identity", buf,
          jsonIdentical ? nullptr : "JSON differs after round-trip");

    // Simulate gold edit
    root["party"]["_gold"] = 99999;
    std::string editedJson = root.dump();
    std::string reEncoded = encodeMZ(editedJson);
    std::string roundTripJson = decodeMZ(reEncoded);
    json roundTripRoot = json::parse(roundTripJson);
    std::string roundTripDump = roundTripRoot.dump();
    bool goldOk = (roundTripRoot["party"]["_gold"].get<int>() == 99999);
    bool jsonMatch = (editedJson == roundTripDump);

    snprintf(buf, sizeof(buf), "edited %zu B -> encode %zu B -> decode %zu B", editedJson.size(), reEncoded.size(), roundTripJson.size());
    check(goldOk && jsonMatch, "edit+round-trip", buf);
    if (!goldOk) info("gold value mismatch");
    if (!jsonMatch) info("JSON differs after full round-trip");

    // Simulate what the game does: UTF-8 decode -> inflate -> parse
    std::vector<unsigned char> gameBytes;
    for (size_t i = 0; i < reEncoded.size(); ) {
        unsigned char c = static_cast<unsigned char>(reEncoded[i]);
        if (c < 0x80) { gameBytes.push_back(c); ++i; }
        else if ((c & 0xE0) == 0xC0) {
            uint32_t cp = (c & 0x1F) << 6;
            cp |= (static_cast<unsigned char>(reEncoded[i+1]) & 0x3F);
            gameBytes.push_back(static_cast<unsigned char>(cp));
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            uint32_t cp = (c & 0x0F) << 12;
            cp |= (static_cast<unsigned char>(reEncoded[i+1]) & 0x3F) << 6;
            cp |= (static_cast<unsigned char>(reEncoded[i+2]) & 0x3F);
            gameBytes.push_back(static_cast<unsigned char>(cp));
            i += 3;
        } else { i++; }
    }

    z_stream strm{};
    inflateInit(&strm);
    strm.next_in = gameBytes.data();
    strm.avail_in = static_cast<uInt>(gameBytes.size());
    std::string gameJson;
    unsigned char obuf[16384];
    int ret;
    bool inflateOk = true;
    do {
        strm.next_out = obuf;
        strm.avail_out = sizeof(obuf);
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            inflateOk = false;
            break;
        }
        gameJson.append(reinterpret_cast<char*>(obuf), sizeof(obuf) - strm.avail_out);
    } while (ret != Z_STREAM_END);
    inflateEnd(&strm);
    gameJson.resize(strm.total_out);
    bool gameValid = inflateOk && (gameJson == editedJson);

    snprintf(buf, sizeof(buf), "inflate %zu B -> JSON %s", gameJson.size(), gameValid ? "valid" : "INVALID");
    check(gameValid, "game inflate", buf,
          gameValid ? nullptr : "pako.inflate simulation produced invalid JSON");

    printf("\n%s=== %d passed, %d failed ===%s\n",
        failures ? C_RED : C_GREEN, passes, failures, C_RESET);
    return failures > 0 ? 1 : 0;
}
