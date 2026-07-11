#include "rpgsave.h"
#include "logger.h"

#include <QFile>
#include <QFileInfo>

static int try_write_stoi(const std::string& s, int fallback = 0)
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
    return SaveFormat::Unknown;
}

// --- Save ---

bool RPGSave::saveToFile(const QString& filePath)
{
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
        case SaveFormat::MvMz:
            encoded = encodeSaveData(jsonStr);
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

// --- Variable setters ---

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

// --- Party setters ---

void RPGSave::setGold(int amount)
{
    if (m_root.contains("party")) {
        m_root["party"]["_gold"] = amount;
        emit modified();
    }
}

void RPGSave::setSteps(int amount)
{
    if (m_root.contains("party")) {
        m_root["party"]["_steps"] = amount;
        emit modified();
    }
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

void RPGSave::setWeapon(int weaponId, int quantity)
{
    if (!m_root.contains("party")) return;
    if (!m_root["party"].contains("_weapons") || !m_root["party"]["_weapons"].is_object()) {
        m_root["party"]["_weapons"] = json::object();
    }
    m_root["party"]["_weapons"][std::to_string(weaponId)] = quantity;
    emit modified();
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

// --- Actor setter ---

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

// --- Export ---

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
