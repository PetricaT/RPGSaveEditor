#pragma once

#include <QObject>
#include <QString>
#include <QMap>
#include <QVector>
#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::ordered_json;

class RPGSave : public QObject {
    Q_OBJECT
public:
    struct Locale {
        QString name; // e.g. "en_US", "ja_JP"
        QMap<int, QString> variableNames;
        QMap<int, QString> switchNames;
    };

    explicit RPGSave(QObject* parent = nullptr);
    ~RPGSave() override = default;

    bool loadFromFile(const QString& filePath);
    bool saveToFile(const QString& filePath);
    bool loadGameData(const QString& systemJsonPath, const QString& itemsJsonPath);

    // Translation loading - scans a directory for translation JSON files,
    // or loads a specific file. Format:
    // { "variables": { "1": "English Name", ... }, "switches": { "1": "English Name", ... } }
    bool loadTranslations(const QString& path);
    void scanForTranslations(const QString& gameRoot);
    QStringList availableLocales() const;
    void setActiveLocale(const QString& locale);
    QString activeLocale() const { return m_activeLocale; }

    bool isLoaded() const { return m_bLoaded; }
    QString filePath() const { return m_filePath; }
    QString errorString() const { return m_error; }
    void reset();

    json& root() { return m_root; }
    const json& root() const { return m_root; }

    // Variable access (1-indexed, RPG Maker convention)
    json getVariable(int id) const;
    void setVariable(int id, const json& value);
    QString variableName(int id) const;
    int variableCount() const;

    // Switch access (1-indexed)
    bool getSwitch(int id) const;
    void setSwitch(int id, bool value);
    QString switchName(int id) const;
    int switchCount() const;

    // Party access
    int gold() const;
    void setGold(int amount);
    int steps() const;
    void setSteps(int amount);
    QMap<int, int> items() const;
    void setItem(int itemId, int quantity);
    QMap<int, int> weapons() const;
    void setWeapon(int weaponId, int quantity);
    QMap<int, int> armors() const;
    void setArmor(int armorId, int quantity);

    // Actor access (1-indexed)
    int actorCount() const;
    json getActor(int id) const;
    void setActor(int id, const json& actor);

    // Maximum IDs from game data (for showing all possible items)
    int maxItemId() const { return m_iMaxItemId; }
    int maxWeaponId() const { return m_iMaxWeaponId; }
    int maxArmorId() const { return m_iMaxArmorId; }
    int maxActorId() const { return m_iMaxActorId; }

    // Item/weapon/armor name lookups
    QString itemName(int id) const;
    QString weaponName(int id) const;
    QString armorName(int id) const;
    QString actorName(int id) const;

    // Export a translation template JSON (native names ready for translation)
    bool exportTranslationTemplate(const QString& path);

    // Save metadata from global.rpgsave
    json saveSlotsMeta() const;

signals:
    void modified();
    void localeChanged(const QString& locale);

private:
    // Extract sparse array data from RPG Maker's @a/@c format into a flat vector.
    // Handles both MV (@a/@c object) and MZ (plain array) formats.
    static std::vector<json> extractSparseArray(const json& obj);
    // Build array data for writing back. MZ uses plain arrays, MV uses @a/@c.
    json buildSparseArray(const std::vector<json>& data) const;

    // MV: LZString compressToBase64
    static std::string decodeSaveData_MV(const std::string& raw);
    static std::string encodeSaveData_MV(const std::string& jsonStr);
    // MZ: pako deflate (UTF-8 encoded byte stream -> zlib inflate)
    static std::string decodeSaveData_MZ(const std::string& raw);
    static std::string encodeSaveData_MZ(const std::string& jsonStr);

    // RPG Maker MV puts raw control chars (0x00-0x1F) inside JSON strings for
    // in-game text formatting (\C[color], etc). JSON spec requires them escaped.
    // This escapes any bare control chars inside string values.
    static std::string sanitizeJson(const std::string& input);

    // Build display names from active locale, falling back to native names
    void rebuildDisplayNames();

    bool m_bLoaded = false;
    bool m_bMZFormat = false;  // true = RPG Maker MZ (pako/zlib), false = RPG Maker MV (LZString)
    QString m_filePath;
    QString m_error;
    json m_root;

    // Native names from System.json (original language, e.g. Japanese)
    QMap<int, QString> m_nativeVariableNames;
    QMap<int, QString> m_nativeSwitchNames;

    // Translated locales: locale_name -> Locale
    QMap<QString, Locale> m_locales;
    QString m_activeLocale;

    // Current display names (rebuilt when locale changes)
    QMap<int, QString> m_variableNames;
    QMap<int, QString> m_switchNames;

    // Maximum valid IDs from Items/Weapons/Armors/Actors.json
    int m_iMaxItemId = 0;
    int m_iMaxWeaponId = 0;
    int m_iMaxArmorId = 0;
    int m_iMaxActorId = 0;

    // Item/weapon/armor/actor names (always from game data, not translated)
    QMap<int, QString> m_itemNames;
    QMap<int, QString> m_weaponNames;
    QMap<int, QString> m_armorNames;
    QMap<int, QString> m_actorNames;
};
