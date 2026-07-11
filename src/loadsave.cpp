#include "rpgsave.h"
#include "logger.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <fstream>

static int try_load_stoi(const std::string& s, int iFallback = 0){
    try {
        return std::stoi(s);
    } catch (...) {
        return iFallback;
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

static SaveFormat detectFormat(const QString& filePath){
    QString suffix = QFileInfo(filePath).suffix().toLower();
    if (suffix == "rpgsave" || suffix == "rmmzsave")
        return SaveFormat::MvMz;
    // Future: add more formats here
    return SaveFormat::Unknown;
}

// --- Load ---

bool RPGSave::loadFromFile(const QString& filePath){
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
    m_bLoaded = true;
    return true;
}

// --- Game data loading ---

bool RPGSave::loadGameData(const QString& systemJsonPath, const QString& itemsJsonPath){
    QFile sysFile(systemJsonPath);
    if (sysFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        std::string data = sysFile.readAll().toStdString();
        sysFile.close();

        try {
            json sysRoot = json::parse(data);

            if (sysRoot.contains("variables") && sysRoot["variables"].is_array()) {
                const auto& aVars = sysRoot["variables"];
                for (size_t i = 1; i < aVars.size(); ++i) {
                    if (aVars[i].is_string() && !aVars[i].get<std::string>().empty()) {
                        m_nativeVariableNames[static_cast<int>(i)] =
                            QString::fromStdString(aVars[i].get<std::string>());
                    }
                }
            }

            if (sysRoot.contains("switches") && sysRoot["switches"].is_array()) {
                const auto& aSws = sysRoot["switches"];
                for (size_t i = 1; i < aSws.size(); ++i) {
                    if (aSws[i].is_string() && !aSws[i].get<std::string>().empty()) {
                        m_nativeSwitchNames[static_cast<int>(i)] =
                            QString::fromStdString(aSws[i].get<std::string>());
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
                m_iMaxItemId = static_cast<int>(itemsRoot.size()) - 1;
                for (size_t i = 1; i < itemsRoot.size(); ++i) {
                    const auto& aNameVal = itemsRoot[i]["name"];
                    if (aNameVal.is_string() && !aNameVal.get<std::string>().empty()) {
                        m_itemNames[static_cast<int>(i)] =
                            QString::fromStdString(aNameVal.get<std::string>());
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
                m_iMaxWeaponId = static_cast<int>(weapRoot.size()) - 1;
                for (size_t i = 1; i < weapRoot.size(); ++i) {
                    const auto& aNameVal = weapRoot[i]["name"];
                    if (aNameVal.is_string() && !aNameVal.get<std::string>().empty()) {
                        m_weaponNames[static_cast<int>(i)] =
                            QString::fromStdString(aNameVal.get<std::string>());
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
                m_iMaxArmorId = static_cast<int>(armRoot.size()) - 1;
                for (size_t i = 1; i < armRoot.size(); ++i) {
                    const auto& aNameVal = armRoot[i]["name"];
                    if (aNameVal.is_string() && !aNameVal.get<std::string>().empty()) {
                        m_armorNames[static_cast<int>(i)] =
                            QString::fromStdString(aNameVal.get<std::string>());
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
                m_iMaxActorId = static_cast<int>(actRoot.size()) - 1;
                for (size_t i = 1; i < actRoot.size(); ++i) {
                    const auto& aNameVal = actRoot[i]["name"];
                    if (aNameVal.is_string() && !aNameVal.get<std::string>().empty()) {
                        m_actorNames[static_cast<int>(i)] =
                            QString::fromStdString(aNameVal.get<std::string>());
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

bool RPGSave::loadTranslations(const QString& path){
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
            for (auto aIt = root["variables"].begin(); aIt != root["variables"].end(); ++aIt) {
                int iId = try_load_stoi(aIt.key());
                loc.variableNames[iId] = QString::fromStdString(aIt.value().get<std::string>());
            }
        }

        if (root.contains("switches") && root["switches"].is_object()) {
            for (auto aIt = root["switches"].begin(); aIt != root["switches"].end(); ++aIt) {
                int iId = try_load_stoi(aIt.key());
                loc.switchNames[iId] = QString::fromStdString(aIt.value().get<std::string>());
            }
        }

        m_locales[localeName] = loc;
        rebuildDisplayNames();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void RPGSave::scanForTranslations(const QString& gameRoot){
    QDir localesDir(gameRoot + "/locales");
    if (!localesDir.exists()) {
        localesDir = QDir(gameRoot + "/www/data/locales");
        if (!localesDir.exists()) {
            localesDir = QDir(gameRoot + "/data/locales");
            if (!localesDir.exists()) return;
        }
    }

    const auto aFiles = localesDir.entryInfoList({"*.json"}, QDir::Files, QDir::Name);
    for (const auto& aFi : aFiles) {
        loadTranslations(aFi.absoluteFilePath());
    }
}

QStringList RPGSave::availableLocales() const{
    QStringList locales;
    locales << "native";
    for (auto aIt = m_locales.constBegin(); aIt != m_locales.constEnd(); ++aIt) {
        locales << aIt.key();
    }
    return locales;
}

void RPGSave::setActiveLocale(const QString& locale){
    m_activeLocale = locale;
    rebuildDisplayNames();
    emit localeChanged(locale);
}

void RPGSave::rebuildDisplayNames(){
    m_variableNames.clear();
    m_switchNames.clear();

    m_variableNames = m_nativeVariableNames;
    m_switchNames = m_nativeSwitchNames;

    if (m_activeLocale != "native" && m_locales.contains(m_activeLocale)) {
        const auto& aLoc = m_locales[m_activeLocale];

        for (auto aIt = aLoc.variableNames.constBegin(); aIt != aLoc.variableNames.constEnd(); ++aIt) {
            m_variableNames[aIt.key()] = aIt.value();
        }
        for (auto aIt = aLoc.switchNames.constBegin(); aIt != aLoc.switchNames.constEnd(); ++aIt) {
            m_switchNames[aIt.key()] = aIt.value();
        }
    }
}

// --- Variable access ---

json RPGSave::getVariable(int iId) const{
    if (!m_root.contains("variables")) return nullptr;
    auto aArr = extractSparseArray(m_root["variables"]);
    if (iId >= 0 && iId < static_cast<int>(aArr.size())) {
        return aArr[iId];
    }
    return nullptr;
}

QString RPGSave::variableName(int iId) const{
    return m_variableNames.value(iId, QString("Var %1").arg(iId));
}

int RPGSave::variableCount() const{
    if (!m_root.contains("variables")) return 0;
    auto aArr = extractSparseArray(m_root["variables"]);
    return static_cast<int>(aArr.size());
}

// --- Switch access ---

bool RPGSave::getSwitch(int iId) const{
    if (!m_root.contains("switches")) return false;
    auto aArr = extractSparseArray(m_root["switches"]);
    if (iId >= 0 && iId < static_cast<int>(aArr.size())) {
        return aArr[iId].is_boolean() ? aArr[iId].get<bool>() : false;
    }
    return false;
}

QString RPGSave::switchName(int iId) const{
    return m_switchNames.value(iId, QString("Switch %1").arg(iId));
}

int RPGSave::switchCount() const{
    if (!m_root.contains("switches")) return 0;
    auto aArr = extractSparseArray(m_root["switches"]);
    return static_cast<int>(aArr.size());
}

// --- Party access (read) ---

int RPGSave::gold() const{
    try {
        if (!m_root.contains("party")) return 0;
        const auto& aP = m_root["party"];
        if (!aP.contains("_gold")) return 0;
        const auto& aG = aP["_gold"];
        return aG.is_number_integer() ? aG.get<int>() : 0;
    } catch (...) {
        return 0;
    }
}

int RPGSave::steps() const{
    try {
        if (!m_root.contains("party")) return 0;
        const auto& aP = m_root["party"];
        if (!aP.contains("_steps")) return 0;
        const auto& aS = aP["_steps"];
        return aS.is_number_integer() ? aS.get<int>() : 0;
    } catch (...) {
        return 0;
    }
}

QMap<int, int> RPGSave::items() const{
    QMap<int, int> result;
    if (m_iMaxItemId <= 0) {
        try {
            if (!m_root.contains("party")) return result;
            const auto& aItemsObj = m_root["party"].value("_items", json::object());
            if (!aItemsObj.is_object()) return result;
            for (auto aIt = aItemsObj.begin(); aIt != aItemsObj.end(); ++aIt) {
                int iId = try_load_stoi(aIt.key());
                int iQty = aIt.value().is_number_integer() ? aIt.value().get<int>() : 0;
                if (iQty > 0) result[iId] = iQty;
            }
        } catch (...) {}
        return result;
    }
    QMap<int, int> saveQtys;
    try {
        if (m_root.contains("party")) {
            const auto& aItemsObj = m_root["party"].value("_items", json::object());
            if (aItemsObj.is_object()) {
                for (auto aIt = aItemsObj.begin(); aIt != aItemsObj.end(); ++aIt) {
                    if (aIt.key() == "@c") continue;
                    int iId = try_load_stoi(aIt.key());
                    int iQty = aIt.value().is_number_integer() ? aIt.value().get<int>() : 0;
                    saveQtys[iId] = iQty;
                }
            }
        }
    } catch (...) {}
    for (int iId = 1; iId <= m_iMaxItemId; ++iId) {
        if (m_itemNames.contains(iId) || saveQtys.value(iId, 0) > 0)
            result[iId] = saveQtys.value(iId, 0);
    }
    return result;
}

QMap<int, int> RPGSave::weapons() const{
    QMap<int, int> result;
    if (m_iMaxWeaponId <= 0) {
        try {
            if (!m_root.contains("party")) return result;
            const auto& aW = m_root["party"].value("_weapons", json::object());
            if (!aW.is_object()) return result;
            for (auto aIt = aW.begin(); aIt != aW.end(); ++aIt) {
                if (aIt.key() == "@c") continue;
                int iId = try_load_stoi(aIt.key());
                int iQty = aIt.value().is_number_integer() ? aIt.value().get<int>() : 0;
                if (iQty > 0) result[iId] = iQty;
            }
        } catch (...) {}
        return result;
    }
    QMap<int, int> saveQtys;
    try {
        if (m_root.contains("party")) {
            const auto& aW = m_root["party"].value("_weapons", json::object());
            if (aW.is_object()) {
                for (auto aIt = aW.begin(); aIt != aW.end(); ++aIt) {
                    if (aIt.key() == "@c") continue;
                    int iId = try_load_stoi(aIt.key());
                    int iQty = aIt.value().is_number_integer() ? aIt.value().get<int>() : 0;
                    saveQtys[iId] = iQty;
                }
            }
        }
    } catch (...) {}
    for (int iId = 1; iId <= m_iMaxWeaponId; ++iId) {
        if (m_weaponNames.contains(iId) || saveQtys.value(iId, 0) > 0)
            result[iId] = saveQtys.value(iId, 0);
    }
    return result;
}

QMap<int, int> RPGSave::armors() const{
    QMap<int, int> result;
    if (m_iMaxArmorId <= 0) {
        try {
            if (!m_root.contains("party")) return result;
            const auto& aA = m_root["party"].value("_armors", json::object());
            if (!aA.is_object()) return result;
            for (auto aIt = aA.begin(); aIt != aA.end(); ++aIt) {
                if (aIt.key() == "@c") continue;
                int iId = try_load_stoi(aIt.key());
                int iQty = aIt.value().is_number_integer() ? aIt.value().get<int>() : 0;
                if (iQty > 0) result[iId] = iQty;
            }
        } catch (...) {}
        return result;
    }
    QMap<int, int> saveQtys;
    try {
        if (m_root.contains("party")) {
            const auto& aA = m_root["party"].value("_armors", json::object());
            if (aA.is_object()) {
                for (auto aIt = aA.begin(); aIt != aA.end(); ++aIt) {
                    if (aIt.key() == "@c") continue;
                    int iId = try_load_stoi(aIt.key());
                    int iQty = aIt.value().is_number_integer() ? aIt.value().get<int>() : 0;
                    saveQtys[iId] = iQty;
                }
            }
        }
    } catch (...) {}
    for (int iId = 1; iId <= m_iMaxArmorId; ++iId) {
        if (m_armorNames.contains(iId) || saveQtys.value(iId, 0) > 0)
            result[iId] = saveQtys.value(iId, 0);
    }
    return result;
}

// --- Actor access ---

int RPGSave::actorCount() const{
    if (!m_root.contains("actors")) return 0;
    auto aArr = extractSparseArray(m_root["actors"]);
    int iCount = 0;
    for (size_t i = 1; i < aArr.size(); ++i) {
        if (!aArr[i].is_null()) iCount++;
    }
    return iCount;
}

json RPGSave::getActor(int iId) const{
    if (!m_root.contains("actors")) return nullptr;
    auto aArr = extractSparseArray(m_root["actors"]);
    if (iId >= 0 && iId < static_cast<int>(aArr.size())) {
        return aArr[iId];
    }
    return nullptr;
}

// --- Name lookups ---

QString RPGSave::itemName(int iId) const{
    return m_itemNames.value(iId, QString("Item %1").arg(iId));
}

QString RPGSave::weaponName(int iId) const{
    return m_weaponNames.value(iId, QString("Weapon %1").arg(iId));
}

QString RPGSave::armorName(int iId) const{
    return m_armorNames.value(iId, QString("Armor %1").arg(iId));
}

QString RPGSave::actorName(int iId) const{
    return m_actorNames.value(iId, QString("Actor %1").arg(iId));
}

// --- Metadata ---

json RPGSave::saveSlotsMeta() const{
    try {
        return m_root;
    } catch (...) {
        return nullptr;
    }
}
