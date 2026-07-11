#include "rpgsave.h"
#include "logger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <fstream>

static int try_load_stoi(const std::string& s, int fallback = 0)
{
    try {
        return std::stoi(s);
    } catch (...) {
        return fallback;
    }
}

// --- Format detection ---

enum class SaveFormat {
    Unknown,
    MvMz,       // .rpgsave, .rmmzsave (LZString base64)
    // Future formats:
    // VxAce,     // .rvdata2 (Ruby Marshal)
    // Vx,        // .rvdata  (Ruby Marshal)
    // Xp,        // .rxdata (Ruby Marshal)
    // Rm2k,      // .lsd    (LSD binary)
};

static SaveFormat detectFormat(const QString& filePath)
{
    QString suffix = QFileInfo(filePath).suffix().toLower();
    if (suffix == "rpgsave" || suffix == "rmmzsave")
        return SaveFormat::MvMz;
    // Future: add more formats here
    return SaveFormat::Unknown;
}

// --- Load ---

bool RPGSave::loadFromFile(const QString& filePath)
{
    LOG_DEBUG("loadFromFile: {}", filePath.toStdString());

    SaveFormat format = detectFormat(filePath);
    if (format == SaveFormat::Unknown) {
        m_error = QString("Unknown save file format: %1").arg(QFileInfo(filePath).fileName());
        LOG_ERROR("{}", m_error.toStdString());
        return false;
    }

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
        switch (format) {
        case SaveFormat::MvMz:
            jsonStr = decodeSaveData(rawData);
            break;
        default:
            break;
        }
    } catch (const std::exception& e) {
        m_error = QString("Decode failed: %1").arg(e.what());
        LOG_ERROR("{}", m_error.toStdString());
        return false;
    }

    if (jsonStr.empty()) {
        m_error = "Decompression returned empty data";
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

// --- Game data loading ---

bool RPGSave::loadGameData(const QString& systemJsonPath, const QString& itemsJsonPath)
{
    QFile sysFile(systemJsonPath);
    if (sysFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::string data = sysFile.readAll().toStdString();
        sysFile.close();

        try {
            json sysRoot = json::parse(data);

            if (sysRoot.contains("variables") && sysRoot["variables"].is_array()) {
                const auto& vars = sysRoot["variables"];
                for (size_t i = 1; i < vars.size(); ++i) {
                    if (vars[i].is_string() && !vars[i].get<std::string>().empty()) {
                        m_nativeVariableNames[static_cast<int>(i)] =
                            QString::fromStdString(vars[i].get<std::string>());
                    }
                }
            }

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
        }
    }

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

// --- Locale / Translation ---

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

        QFileInfo fi(path);
        QString localeName = fi.completeBaseName();

        Locale loc;
        loc.name = localeName;

        if (root.contains("variables") && root["variables"].is_object()) {
            for (auto it = root["variables"].begin(); it != root["variables"].end(); ++it) {
                int id = try_load_stoi(it.key());
                loc.variableNames[id] = QString::fromStdString(it.value().get<std::string>());
            }
        }

        if (root.contains("switches") && root["switches"].is_object()) {
            for (auto it = root["switches"].begin(); it != root["switches"].end(); ++it) {
                int id = try_load_stoi(it.key());
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
    QDir localesDir(gameRoot + "/locales");
    if (!localesDir.exists()) {
        localesDir = QDir(gameRoot + "/www/data/locales");
        if (!localesDir.exists()) {
            localesDir = QDir(gameRoot + "/data/locales");
            if (!localesDir.exists()) return;
        }
    }

    const auto files = localesDir.entryInfoList({"*.json"}, QDir::Files, QDir::Name);
    for (const auto& fi : files) {
        loadTranslations(fi.absoluteFilePath());
    }
}

QStringList RPGSave::availableLocales() const
{
    QStringList locales;
    locales << "native";
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

    m_variableNames = m_nativeVariableNames;
    m_switchNames = m_nativeSwitchNames;

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

// --- Variable access ---

json RPGSave::getVariable(int id) const
{
    if (!m_root.contains("variables")) return nullptr;
    auto arr = extractSparseArray(m_root["variables"]);
    if (id >= 0 && id < static_cast<int>(arr.size())) {
        return arr[id];
    }
    return nullptr;
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

// --- Switch access ---

bool RPGSave::getSwitch(int id) const
{
    if (!m_root.contains("switches")) return false;
    auto arr = extractSparseArray(m_root["switches"]);
    if (id >= 0 && id < static_cast<int>(arr.size())) {
        return arr[id].is_boolean() ? arr[id].get<bool>() : false;
    }
    return false;
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

// --- Party access (read) ---

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

QMap<int, int> RPGSave::items() const
{
    QMap<int, int> result;
    if (m_maxItemId <= 0) {
        try {
            if (!m_root.contains("party")) return result;
            const auto& itemsObj = m_root["party"].value("_items", json::object());
            if (!itemsObj.is_object()) return result;
            for (auto it = itemsObj.begin(); it != itemsObj.end(); ++it) {
                int id = try_load_stoi(it.key());
                int qty = it.value().is_number_integer() ? it.value().get<int>() : 0;
                if (qty > 0) result[id] = qty;
            }
        } catch (...) {}
        return result;
    }
    QMap<int, int> saveQtys;
    try {
        if (m_root.contains("party")) {
            const auto& itemsObj = m_root["party"].value("_items", json::object());
            if (itemsObj.is_object()) {
                for (auto it = itemsObj.begin(); it != itemsObj.end(); ++it) {
                    if (it.key() == "@c") continue;
                    int id = try_load_stoi(it.key());
                    int qty = it.value().is_number_integer() ? it.value().get<int>() : 0;
                    saveQtys[id] = qty;
                }
            }
        }
    } catch (...) {}
    for (int id = 1; id <= m_maxItemId; ++id) {
        if (m_itemNames.contains(id) || saveQtys.value(id, 0) > 0)
            result[id] = saveQtys.value(id, 0);
    }
    return result;
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
                int id = try_load_stoi(it.key());
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
                    int id = try_load_stoi(it.key());
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
                int id = try_load_stoi(it.key());
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
                    int id = try_load_stoi(it.key());
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

// --- Metadata ---

json RPGSave::saveSlotsMeta() const
{
    try {
        return m_root;
    } catch (...) {
        return nullptr;
    }
}
