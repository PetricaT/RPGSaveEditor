#include "rpgsave.h"
#include "logger.h"

#include <QFile>
#include <QFileInfo>

static int try_write_stoi(const std::string& s, int iFallback = 0){
    try {
        return std::stoi(s);
    } catch (...) {
        return iFallback;
    }
}

// --- Format detection ---

enum class SaveFormat {
    Unknown,
    Mv,         // .rpgsave (LZString compressToBase64)
    Mz,         // .rmmzsave (pako deflate, UTF-8 encoded byte stream)
    // Future formats:
    // VxAce,     // .rvdata2 (Ruby Marshal)
    // Vx,        // .rvdata  (Ruby Marshal)
    // Xp,        // .rxdata (Ruby Marshal)
    // Rm2k,      // .lsd    (LSD binary)
};

static SaveFormat detectFormat(const QString& filePath){
    QString suffix = QFileInfo(filePath).suffix().toLower();
    if (suffix == "rpgsave")
        return SaveFormat::Mv;
    if (suffix == "rmmzsave")
        return SaveFormat::Mz;
    return SaveFormat::Unknown;
}

// --- Save ---

bool RPGSave::saveToFile(const QString& filePath){
    LOG_DEBUG("saveToFile: {}", filePath.toStdString());

    SaveFormat format = detectFormat(filePath);
    if (format == SaveFormat::Unknown) {
        m_error = QString("Unknown save file format: %1").arg(QFileInfo(filePath).fileName());
        LOG_ERROR("{}", m_error.toStdString());
        return false;
    }

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
        switch (format) {
        case SaveFormat::Mv:
            encoded = encodeSaveData_MV(jsonStr);
            break;
        case SaveFormat::Mz:
            encoded = encodeSaveData_MZ(jsonStr);
            break;
        default:
            break;
        }
        LOG_DEBUG("  compressed size: {} bytes", encoded.size());
    } catch (const std::exception& e) {
        m_error = QString("Encode failed: %1").arg(e.what());
        LOG_ERROR("{}", m_error.toStdString());
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
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

// --- Variable setters ---

void RPGSave::setVariable(int iId, const json& value){
    if (!m_root.contains("variables")) return;
    auto aArr = extractSparseArray(m_root["variables"]);
    if (iId >= 0 && iId < static_cast<int>(aArr.size())) {
        aArr[iId] = value;
        m_root["variables"]["_data"] = buildSparseArray(aArr);
        emit modified();
    }
}

void RPGSave::setSwitch(int iId, bool bValue){
    if (!m_root.contains("switches")) return;
    auto aArr = extractSparseArray(m_root["switches"]);
    if (iId >= 0 && iId < static_cast<int>(aArr.size())) {
        aArr[iId] = bValue;
        m_root["switches"]["_data"] = buildSparseArray(aArr);
        emit modified();
    }
}

// --- Party setters ---

void RPGSave::setGold(int iAmount){
    if (m_root.contains("party")) {
        m_root["party"]["_gold"] = iAmount;
        emit modified();
    }
}

void RPGSave::setSteps(int iAmount){
    if (m_root.contains("party")) {
        m_root["party"]["_steps"] = iAmount;
        emit modified();
    }
}

void RPGSave::setItem(int iItemId, int iQuantity){
    if (!m_root.contains("party")) return;
    if (!m_root["party"].contains("_items") || !m_root["party"]["_items"].is_object()) {
        m_root["party"]["_items"] = json::object();
    }
    m_root["party"]["_items"][std::to_string(iItemId)] = iQuantity;
    emit modified();
}

void RPGSave::setWeapon(int iWeaponId, int iQuantity){
    if (!m_root.contains("party")) return;
    if (!m_root["party"].contains("_weapons") || !m_root["party"]["_weapons"].is_object()) {
        m_root["party"]["_weapons"] = json::object();
    }
    m_root["party"]["_weapons"][std::to_string(iWeaponId)] = iQuantity;
    emit modified();
}

void RPGSave::setArmor(int iArmorId, int iQuantity){
    if (!m_root.contains("party")) return;
    if (!m_root["party"].contains("_armors") || !m_root["party"]["_armors"].is_object()) {
        m_root["party"]["_armors"] = json::object();
    }
    m_root["party"]["_armors"][std::to_string(iArmorId)] = iQuantity;
    emit modified();
}

// --- Actor setter ---

void RPGSave::setActor(int iId, const json& actor){
    if (!m_root.contains("actors")) return;
    auto aArr = extractSparseArray(m_root["actors"]);
    if (iId >= 0 && iId < static_cast<int>(aArr.size())) {
        aArr[iId] = actor;
        m_root["actors"]["_data"] = buildSparseArray(aArr);
        emit modified();
    }
}

// --- Export ---

bool RPGSave::exportTranslationTemplate(const QString& path){
    LOG_INFO("exportTranslationTemplate: {}", path.toStdString());
    json out;
    out["variables"] = json::object();
    out["switches"] = json::object();

    for (auto aIt = m_nativeVariableNames.constBegin(); aIt != m_nativeVariableNames.constEnd(); ++aIt) {
        out["variables"][std::to_string(aIt.key())] = aIt.value().toStdString();
    }
    for (auto aIt = m_nativeSwitchNames.constBegin(); aIt != m_nativeSwitchNames.constEnd(); ++aIt) {
        out["switches"][std::to_string(aIt.key())] = aIt.value().toStdString();
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
