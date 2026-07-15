#include "helpers.h"
#include "logger.h"

#include <QDir>
#include <QFileInfo>

QStringList findRPGSaveFiles(const QString& dirPath){
    QDir dir(dirPath);
    if (!dir.exists()) {
        LOG_WARN("findRPGSaveFiles: directory does not exist: {}", dirPath.toStdString());
        return {};
    }

    QStringList filters = {"file*.rpgsave", "file*.rmmzsave"};
    QStringList files;
    const auto aEntries = dir.entryInfoList(filters, QDir::Files, QDir::Name);
    for (const auto& entry : aEntries) {
        if (!entry.fileName().endsWith(".bak")) {
            files << entry.absoluteFilePath();
            LOG_DEBUG("  found save: {}", entry.absoluteFilePath().toStdString());
        }
    }
    LOG_INFO("findRPGSaveFiles({}): found {} saves", dirPath.toStdString(), files.size());
    return files;
}

QString getSaveSlotPath(const QString& saveDir, int iSlot){
    // Detect whether the save directory uses .rmmzsave or .rpgsave
    QDir dir(saveDir);
    const auto aEntries = dir.entryInfoList({"file*.*save"}, QDir::Files);
    for (const auto& entry : aEntries) {
        if (entry.fileName().endsWith(".rmmzsave"))
            return QString("%1/file%2.rmmzsave").arg(saveDir, QString::number(iSlot));
    }
    return QString("%1/file%2.rpgsave").arg(saveDir, QString::number(iSlot));
}

int getNextSaveSlot(const QString& saveDir){
    QDir dir(saveDir);
    if (!dir.exists()) return 1;

    int iMaxSlot = 0;
    const auto aEntries = dir.entryInfoList({"file*.rpgsave", "file*.rmmzsave"}, QDir::Files, QDir::Name);
    for (const auto& entry : aEntries) {
        if (entry.fileName().endsWith(".bak")) continue;
        QString name = entry.fileName();
        name.remove("file");
        name.remove(".rpgsave");
        name.remove(".rmmzsave");
        bool bOk = false;
        int iSlot = name.toInt(&bOk);
        if (bOk && iSlot > iMaxSlot) iMaxSlot = iSlot;
    }
    int iNext = iMaxSlot + 1;
    LOG_DEBUG("getNextSaveSlot({}): max={}, next={}", saveDir.toStdString(), iMaxSlot, iNext);
    return iNext;
}

QString gameDataPathFromRoot(const QString& gameRoot){
    for (const auto& sub : {"/www/data", "/data"}) {
        QString path = gameRoot + sub;
        if (QDir(path).exists()) {
            LOG_DEBUG("gameDataPath: found at {}", path.toStdString());
            return path;
        }
    }
    LOG_WARN("gameDataPath: NOT found in {}", gameRoot.toStdString());
    return {};
}

QString saveDirFromRoot(const QString& gameRoot){
    for (const auto& sub : {"/www/save", "/save"}) {
        QString path = gameRoot + sub;
        if (QDir(path).exists()) {
            LOG_DEBUG("saveDir: found at {}", path.toStdString());
            return path;
        }
    }
    LOG_WARN("saveDir: NOT found in {}", gameRoot.toStdString());
    return {};
}

bool isRPGMakerGameRoot(const QString& gameRoot){
    QDir dir(gameRoot);
    const auto aFiles = dir.entryList(QDir::Files);

    for (const auto& f : aFiles) {
        QString lower = f.toLower();
        if (lower == "game.exe" || lower == "nw.exe")
            return true;
    }

    bool bHasNwDll = false;
    for (const auto& f : aFiles) {
        if (f.toLower() == "nw.dll") { bHasNwDll = true; break; }
    }
    if (bHasNwDll) {
        for (const auto& f : aFiles) {
            if (f.toLower().endsWith(".exe") && f.toLower() != "unins000.exe")
                return true;
        }
    }

    QStringList saveExtensions = {
        ".rpgsave", ".rmmzsave", ".rvdata2", ".rvdata", ".rxdata", ".lsd", ".sav"
    };
    QString saveDir = saveDirFromRoot(gameRoot);
    if (!saveDir.isEmpty()) {
        QDir sd(saveDir);
        const auto aSaveFiles = sd.entryList(QDir::Files);
        for (const auto& f : aSaveFiles) {
            for (const auto& ext : saveExtensions) {
                if (f.toLower().endsWith(ext))
                    return true;
            }
        }
    }

    return false;
}
