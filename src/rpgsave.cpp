#include "rpgsave.h"
#include "lz_string.hpp"
#include "logger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>

#include <fstream>
#include <sstream>

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

bool RPGSave::loadFromFile(const QString& filePath)
{
    LOG_DEBUG("loadFromFile: {}", filePath.toStdString());
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_error = QString("Cannot open file: %1").arg(file.errorString());
        LOG_ERROR("{}", m_error.toStdString());
        return false;
    }

    std::string rawData = file.readAll().toStdString();
    file.close();
    LOG_DEBUG("  raw file size: {} bytes", rawData.size());

    std::string jsonStr;
    try {
        jsonStr = decodeSaveData(rawData);
    } catch (const std::exception& e) {
        m_error = QString("LZString decode failed: %1").arg(e.what());
        LOG_ERROR("{}", m_error.toStdString());
        return false;
    }

    if (jsonStr.empty()) {
        m_error = "LZString decompression returned empty data";
        LOG_ERROR("{}", m_error.toStdString());
        return false;
    }
    LOG_DEBUG("  decompressed size: {} bytes", jsonStr.size());

    std::string sanitized;
    try {
        sanitized = sanitizeJson(jsonStr);
    } catch (const std::exception& e) {
        m_error = QString("Failed to sanitize JSON: %1").arg(e.what());
        LOG_ERROR("{}", m_error.toStdString());
        return false;
    }

    try {
        m_root = json::parse(sanitized);
    } catch (const json::parse_error& e) {
        m_error = QString("JSON parse error: %1").arg(e.what());
        LOG_ERROR("{}", m_error.toStdString());
        return false;
    }

    LOG_INFO("Save loaded: {} variables, {} switches",
             variableCount(), switchCount());
    m_filePath = filePath;
    m_loaded = true;
    return true;
}

bool RPGSave::saveToFile(const QString& filePath)
{
    LOG_DEBUG("saveToFile: {}", filePath.toStdString());
    std::string jsonStr;
    try {
        jsonStr = m_root.dump();
        LOG_DEBUG("  json size: {} bytes", jsonStr.size());
    } catch (const std::exception& e) {
        m_error = QString("JSON serialize error: %1").arg(e.what());
        LOG_ERROR("{}", m_error.toStdString());
        return false;
    }

    std::string encoded;
    try {
        encoded = encodeSaveData(jsonStr);
        LOG_DEBUG("  compressed size: {} bytes", encoded.size());
    } catch (const std::exception& e) {
        m_error = QString("LZString encode failed: %1").arg(e.what());
        LOG_ERROR("{}", m_error.toStdString());
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_error = QString("Cannot write file: %1").arg(file.errorString());
        LOG_ERROR("{}", m_error.toStdString());
        return false;
    }

    file.write(encoded.c_str(), static_cast<qint64>(encoded.size()));
    file.close();
    LOG_INFO("Saved to: {}", filePath.toStdString());
    m_filePath = filePath;
    return true;
}

bool RPGSave::loadGameData(const QString& systemJsonPath, const QString& itemsJsonPath)
{
    // Load System.json for variable/switch names
    QFile sysFile(systemJsonPath);
    if (sysFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::string data = sysFile.readAll().toStdString();
        sysFile.close();

        try {
            json sysRoot = json::parse(data);

            // Variables array (index = id)
            if (sysRoot.contains("variables") && sysRoot["variables"].is_array()) {
                const auto& vars = sysRoot["variables"];
                for (size_t i = 1; i < vars.size(); ++i) {
                    if (vars[i].is_string() && !vars[i].get<std::string>().empty()) {
                        m_nativeVariableNames[static_cast<int>(i)] =
                            QString::fromStdString(vars[i].get<std::string>());
                    }
                }
            }

            // Switches array (index = id)
            if (sysRoot.contains("switches") && sysRoot["switches"].is_array()) {
                const auto& sws = sysRoot["switches"];
                for (size_t i = 1; i < sws.size(); ++i) {
                    if (sws[i].is_string() && !sws[i].get<std::string>().empty()) {
                        m_nativeSwitchNames[static_cast<int>(i)] =
                            QString::fromStdString(sws[i].get<std::string>());
                    }
                }
            }
        } catch (const std::exception&) {
            // Silently ignore parse errors, names just won't be available
        }
    }

    // Load Items.json
    QFile itemsFile(itemsJsonPath);
    if (itemsFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::string data = itemsFile.readAll().toStdString();
        itemsFile.close();

        try {
            json itemsRoot = json::parse(data);
            if (itemsRoot.is_array()) {
                m_maxItemId = static_cast<int>(itemsRoot.size()) - 1;
                for (size_t i = 1; i < itemsRoot.size(); ++i) {
                    const auto& nameVal = itemsRoot[i]["name"];
                    if (nameVal.is_string() && !nameVal.get<std::string>().empty()) {
                        m_itemNames[static_cast<int>(i)] =
                            QString::fromStdString(nameVal.get<std::string>());
                    }
                }
            }
        } catch (const std::exception&) {
        }
    }

    // Load Weapons.json
    QString weaponsPath = QFileInfo(itemsJsonPath).absolutePath() + "/Weapons.json";
    QFile weapFile(weaponsPath);
    if (weapFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::string data = weapFile.readAll().toStdString();
        weapFile.close();

        try {
            json weapRoot = json::parse(data);
            if (weapRoot.is_array()) {
                m_maxWeaponId = static_cast<int>(weapRoot.size()) - 1;
                for (size_t i = 1; i < weapRoot.size(); ++i) {
                    const auto& nameVal = weapRoot[i]["name"];
                    if (nameVal.is_string() && !nameVal.get<std::string>().empty()) {
                        m_weaponNames[static_cast<int>(i)] =
                            QString::fromStdString(nameVal.get<std::string>());
                    }
                }
            }
        } catch (const std::exception&) {
        }
    }

    // Load Armors.json
    QString armorsPath = QFileInfo(itemsJsonPath).absolutePath() + "/Armors.json";
    QFile armFile(armorsPath);
    if (armFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::string data = armFile.readAll().toStdString();
        armFile.close();

        try {
            json armRoot = json::parse(data);
            if (armRoot.is_array()) {
                m_maxArmorId = static_cast<int>(armRoot.size()) - 1;
                for (size_t i = 1; i < armRoot.size(); ++i) {
                    const auto& nameVal = armRoot[i]["name"];
                    if (nameVal.is_string() && !nameVal.get<std::string>().empty()) {
                        m_armorNames[static_cast<int>(i)] =
                            QString::fromStdString(nameVal.get<std::string>());
                    }
                }
            }
        } catch (const std::exception&) {
        }
    }

    // Load Actors.json
    QString actorsPath = QFileInfo(itemsJsonPath).absolutePath() + "/Actors.json";
    QFile actFile(actorsPath);
    if (actFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::string data = actFile.readAll().toStdString();
        actFile.close();

        try {
            json actRoot = json::parse(data);
            if (actRoot.is_array()) {
                m_maxActorId = static_cast<int>(actRoot.size()) - 1;
                for (size_t i = 1; i < actRoot.size(); ++i) {
                    const auto& nameVal = actRoot[i]["name"];
                    if (nameVal.is_string() && !nameVal.get<std::string>().empty()) {
                        m_actorNames[static_cast<int>(i)] =
                            QString::fromStdString(nameVal.get<std::string>());
                    }
                }
            }
        } catch (const std::exception&) {
        }
    }

    rebuildDisplayNames();

    return true;
}

// --- Variable access ---

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

// --- Locale / Translation methods ---

bool RPGSave::loadTranslations(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    std::string data = file.readAll().toStdString();
    file.close();

    try {
        json root = json::parse(sanitizeJson(data));

        // Determine locale name from filename: "en.json" -> "en", "en_US.json" -> "en_US"
        QFileInfo fi(path);
        QString localeName = fi.completeBaseName();

        Locale loc;
        loc.name = localeName;

        if (root.contains("variables") && root["variables"].is_object()) {
            for (auto it = root["variables"].begin(); it != root["variables"].end(); ++it) {
                int id = try_stoi(it.key());
                loc.variableNames[id] = QString::fromStdString(it.value().get<std::string>());
            }
        }

        if (root.contains("switches") && root["switches"].is_object()) {
            for (auto it = root["switches"].begin(); it != root["switches"].end(); ++it) {
                int id = try_stoi(it.key());
                loc.switchNames[id] = QString::fromStdString(it.value().get<std::string>());
            }
        }

        m_locales[localeName] = loc;
        rebuildDisplayNames();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void RPGSave::scanForTranslations(const QString& gameRoot)
{
    // Scan locales/ directory in the game root
    QDir localesDir(gameRoot + "/locales");
    if (!localesDir.exists()) {
        localesDir = QDir(gameRoot + "/www/data/locales");
        if (!localesDir.exists()) {
            localesDir = QDir(gameRoot + "/data/locales");
            if (!localesDir.exists()) return;
        }
    }

    // Scan for translation JSON files
    const auto files = localesDir.entryInfoList({"*.json"}, QDir::Files, QDir::Name);
    for (const auto& fi : files) {
        loadTranslations(fi.absoluteFilePath());
    }
}

QStringList RPGSave::availableLocales() const
{
    QStringList locales;
    locales << "native"; // native always available
    for (auto it = m_locales.constBegin(); it != m_locales.constEnd(); ++it) {
        locales << it.key();
    }
    return locales;
}

void RPGSave::setActiveLocale(const QString& locale)
{
    m_activeLocale = locale;
    rebuildDisplayNames();
    emit localeChanged(locale);
}

void RPGSave::rebuildDisplayNames()
{
    m_variableNames.clear();
    m_switchNames.clear();

    // Start with native names
    m_variableNames = m_nativeVariableNames;
    m_switchNames = m_nativeSwitchNames;

    // Overlay with translation if a specific locale is set (not "native")
    if (m_activeLocale != "native" && m_locales.contains(m_activeLocale)) {
        const auto& loc = m_locales[m_activeLocale];

        for (auto it = loc.variableNames.constBegin(); it != loc.variableNames.constEnd(); ++it) {
            m_variableNames[it.key()] = it.value();
        }
        for (auto it = loc.switchNames.constBegin(); it != loc.switchNames.constEnd(); ++it) {
            m_switchNames[it.key()] = it.value();
        }
    }
}

json RPGSave::getVariable(int id) const
{
    if (!m_root.contains("variables")) return nullptr;
    auto arr = extractSparseArray(m_root["variables"]);
    if (id >= 0 && id < static_cast<int>(arr.size())) {
        return arr[id];
    }
    return nullptr;
}

void RPGSave::setVariable(int id, const json& value)
{
    if (!m_root.contains("variables")) return;
    auto arr = extractSparseArray(m_root["variables"]);
    if (id >= 0 && id < static_cast<int>(arr.size())) {
        arr[id] = value;
        m_root["variables"]["_data"] = buildSparseArray(arr);
        emit modified();
    }
}

QString RPGSave::variableName(int id) const
{
    return m_variableNames.value(id, QString("Var %1").arg(id));
}

int RPGSave::variableCount() const
{
    if (!m_root.contains("variables")) return 0;
    auto arr = extractSparseArray(m_root["variables"]);
    return static_cast<int>(arr.size());
}

bool RPGSave::getSwitch(int id) const
{
    if (!m_root.contains("switches")) return false;
    auto arr = extractSparseArray(m_root["switches"]);
    if (id >= 0 && id < static_cast<int>(arr.size())) {
        return arr[id].is_boolean() ? arr[id].get<bool>() : false;
    }
    return false;
}

void RPGSave::setSwitch(int id, bool value)
{
    if (!m_root.contains("switches")) return;
    auto arr = extractSparseArray(m_root["switches"]);
    if (id >= 0 && id < static_cast<int>(arr.size())) {
        arr[id] = value;
        m_root["switches"]["_data"] = buildSparseArray(arr);
        emit modified();
    }
}

QString RPGSave::switchName(int id) const
{
    return m_switchNames.value(id, QString("Switch %1").arg(id));
}

int RPGSave::switchCount() const
{
    if (!m_root.contains("switches")) return 0;
    auto arr = extractSparseArray(m_root["switches"]);
    return static_cast<int>(arr.size());
}

// --- Party access ---

int RPGSave::gold() const
{
    try {
        if (!m_root.contains("party")) return 0;
        const auto& p = m_root["party"];
        if (!p.contains("_gold")) return 0;
        const auto& g = p["_gold"];
        return g.is_number_integer() ? g.get<int>() : 0;
    } catch (...) {
        return 0;
    }
}

void RPGSave::setGold(int amount)
{
    if (m_root.contains("party")) {
        m_root["party"]["_gold"] = amount;
        emit modified();
    }
}

int RPGSave::steps() const
{
    try {
        if (!m_root.contains("party")) return 0;
        const auto& p = m_root["party"];
        if (!p.contains("_steps")) return 0;
        const auto& s = p["_steps"];
        return s.is_number_integer() ? s.get<int>() : 0;
    } catch (...) {
        return 0;
    }
}

void RPGSave::setSteps(int amount)
{
    if (m_root.contains("party")) {
        m_root["party"]["_steps"] = amount;
        emit modified();
    }
}

QMap<int, int> RPGSave::items() const
{
    QMap<int, int> result;
    if (m_maxItemId <= 0) {
        // No game data loaded, fall back to save file entries only
        try {
            if (!m_root.contains("party")) return result;
            const auto& itemsObj = m_root["party"].value("_items", json::object());
            if (!itemsObj.is_object()) return result;
            for (auto it = itemsObj.begin(); it != itemsObj.end(); ++it) {
                int id = try_stoi(it.key());
                int qty = it.value().is_number_integer() ? it.value().get<int>() : 0;
                if (qty > 0) result[id] = qty;
            }
        } catch (...) {}
        return result;
    }
    // Load save data into a map
    QMap<int, int> saveQtys;
    try {
        if (m_root.contains("party")) {
            const auto& itemsObj = m_root["party"].value("_items", json::object());
            if (itemsObj.is_object()) {
                for (auto it = itemsObj.begin(); it != itemsObj.end(); ++it) {
                    if (it.key() == "@c") continue;
                    int id = try_stoi(it.key());
                    int qty = it.value().is_number_integer() ? it.value().get<int>() : 0;
                    saveQtys[id] = qty;
                }
            }
        }
    } catch (...) {}
    // Return all possible items with save quantities (0 for missing)
    for (int id = 1; id <= m_maxItemId; ++id) {
        if (m_itemNames.contains(id) || saveQtys.value(id, 0) > 0)
            result[id] = saveQtys.value(id, 0);
    }
    return result;
}

void RPGSave::setItem(int itemId, int quantity)
{
    if (!m_root.contains("party")) return;
    if (!m_root["party"].contains("_items") || !m_root["party"]["_items"].is_object()) {
        m_root["party"]["_items"] = json::object();
    }
    m_root["party"]["_items"][std::to_string(itemId)] = quantity;
    emit modified();
}

QMap<int, int> RPGSave::weapons() const
{
    QMap<int, int> result;
    if (m_maxWeaponId <= 0) {
        try {
            if (!m_root.contains("party")) return result;
            const auto& w = m_root["party"].value("_weapons", json::object());
            if (!w.is_object()) return result;
            for (auto it = w.begin(); it != w.end(); ++it) {
                if (it.key() == "@c") continue;
                int id = try_stoi(it.key());
                int qty = it.value().is_number_integer() ? it.value().get<int>() : 0;
                if (qty > 0) result[id] = qty;
            }
        } catch (...) {}
        return result;
    }
    QMap<int, int> saveQtys;
    try {
        if (m_root.contains("party")) {
            const auto& w = m_root["party"].value("_weapons", json::object());
            if (w.is_object()) {
                for (auto it = w.begin(); it != w.end(); ++it) {
                    if (it.key() == "@c") continue;
                    int id = try_stoi(it.key());
                    int qty = it.value().is_number_integer() ? it.value().get<int>() : 0;
                    saveQtys[id] = qty;
                }
            }
        }
    } catch (...) {}
    for (int id = 1; id <= m_maxWeaponId; ++id) {
        if (m_weaponNames.contains(id) || saveQtys.value(id, 0) > 0)
            result[id] = saveQtys.value(id, 0);
    }
    return result;
}

void RPGSave::setWeapon(int weaponId, int quantity)
{
    if (!m_root.contains("party")) return;
    if (!m_root["party"].contains("_weapons") || !m_root["party"]["_weapons"].is_object()) {
        m_root["party"]["_weapons"] = json::object();
    }
    m_root["party"]["_weapons"][std::to_string(weaponId)] = quantity;
    emit modified();
}

QMap<int, int> RPGSave::armors() const
{
    QMap<int, int> result;
    if (m_maxArmorId <= 0) {
        try {
            if (!m_root.contains("party")) return result;
            const auto& a = m_root["party"].value("_armors", json::object());
            if (!a.is_object()) return result;
            for (auto it = a.begin(); it != a.end(); ++it) {
                if (it.key() == "@c") continue;
                int id = try_stoi(it.key());
                int qty = it.value().is_number_integer() ? it.value().get<int>() : 0;
                if (qty > 0) result[id] = qty;
            }
        } catch (...) {}
        return result;
    }
    QMap<int, int> saveQtys;
    try {
        if (m_root.contains("party")) {
            const auto& a = m_root["party"].value("_armors", json::object());
            if (a.is_object()) {
                for (auto it = a.begin(); it != a.end(); ++it) {
                    if (it.key() == "@c") continue;
                    int id = try_stoi(it.key());
                    int qty = it.value().is_number_integer() ? it.value().get<int>() : 0;
                    saveQtys[id] = qty;
                }
            }
        }
    } catch (...) {}
    for (int id = 1; id <= m_maxArmorId; ++id) {
        if (m_armorNames.contains(id) || saveQtys.value(id, 0) > 0)
            result[id] = saveQtys.value(id, 0);
    }
    return result;
}

void RPGSave::setArmor(int armorId, int quantity)
{
    if (!m_root.contains("party")) return;
    if (!m_root["party"].contains("_armors") || !m_root["party"]["_armors"].is_object()) {
        m_root["party"]["_armors"] = json::object();
    }
    m_root["party"]["_armors"][std::to_string(armorId)] = quantity;
    emit modified();
}

// --- Actor access ---

int RPGSave::actorCount() const
{
    if (!m_root.contains("actors")) return 0;
    auto arr = extractSparseArray(m_root["actors"]);
    int count = 0;
    for (size_t i = 1; i < arr.size(); ++i) {
        if (!arr[i].is_null()) count++;
    }
    return count;
}

json RPGSave::getActor(int id) const
{
    if (!m_root.contains("actors")) return nullptr;
    auto arr = extractSparseArray(m_root["actors"]);
    if (id >= 0 && id < static_cast<int>(arr.size())) {
        return arr[id];
    }
    return nullptr;
}

void RPGSave::setActor(int id, const json& actor)
{
    if (!m_root.contains("actors")) return;
    auto arr = extractSparseArray(m_root["actors"]);
    if (id >= 0 && id < static_cast<int>(arr.size())) {
        arr[id] = actor;
        m_root["actors"]["_data"] = buildSparseArray(arr);
        emit modified();
    }
}

bool RPGSave::exportTranslationTemplate(const QString& path)
{
    LOG_INFO("exportTranslationTemplate: {}", path.toStdString());
    json out;
    out["variables"] = json::object();
    out["switches"] = json::object();

    for (auto it = m_nativeVariableNames.constBegin(); it != m_nativeVariableNames.constEnd(); ++it) {
        out["variables"][std::to_string(it.key())] = it.value().toStdString();
    }
    for (auto it = m_nativeSwitchNames.constBegin(); it != m_nativeSwitchNames.constEnd(); ++it) {
        out["switches"][std::to_string(it.key())] = it.value().toStdString();
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_error = QString("Cannot write translation template: %1").arg(file.errorString());
        LOG_ERROR("{}", m_error.toStdString());
        return false;
    }

    file.write(out.dump(2).c_str());
    file.close();
    LOG_INFO("Translation template exported ({} variables, {} switches)",
             out["variables"].size(), out["switches"].size());
    return true;
}

// --- Name lookups ---

QString RPGSave::itemName(int id) const
{
    return m_itemNames.value(id, QString("Item %1").arg(id));
}

QString RPGSave::weaponName(int id) const
{
    return m_weaponNames.value(id, QString("Weapon %1").arg(id));
}

QString RPGSave::armorName(int id) const
{
    return m_armorNames.value(id, QString("Armor %1").arg(id));
}

QString RPGSave::actorName(int id) const
{
    return m_actorNames.value(id, QString("Actor %1").arg(id));
}

json RPGSave::saveSlotsMeta() const
{
    try {
        return m_root;
    } catch (...) {
        return nullptr;
    }
}

// --- LZString encode/decode ---

// Convert UTF-16 (char16_t) to UTF-8 (char) properly, handling surrogate pairs.
// Handles both LE/BE by interpreting the char16_t as UTF-16 code units.
static std::string utf16_to_utf8(const lzstring::string& input)
{
    std::string result;
    for (size_t i = 0; i < input.size(); ++i) {
        uint32_t cp = static_cast<uint32_t>(input[i]);

        // Surrogate pair: high surrogate (0xD800-0xDBFF) + low surrogate (0xDC00-0xDFFF)
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < input.size()) {
            uint32_t low = static_cast<uint32_t>(input[i + 1]);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            }
        }

        // Encode codepoint to UTF-8
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
            ++i; // invalid byte, skip
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

std::string RPGSave::sanitizeJson(const std::string& input)
{
    // RPG Maker MV embeds raw control characters (0x00-0x1F) inside JSON strings
    // for in-game text formatting (\C[color], \N[name], etc). The JSON spec
    // requires these to be escaped as \uXXXX. Walk the string tracking whether
    // we're inside a JSON string literal, and escape any bare control chars.
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

        // Inside a JSON string
        if (c == '\\' && i + 1 < input.size()) {
            // Escape sequence - pass backslash + next char through
            output += input[i];
            ++i;
            output += input[i];
            unsigned char next = static_cast<unsigned char>(input[i]);
            // \uXXXX needs 4 more hex digits
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
            // Bare control character - escape it
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", c);
            output += buf;
        } else {
            output += input[i];
        }
    }

    return output;
}
