#include "helpers.h"
#include "logger.h"

#include <QDir>
#include <QFileInfo>

QStringList findRPGSaveFiles(const QString& dirPath)
{
    QDir dir(dirPath);
    if (!dir.exists()) {
        LOG_WARN("findRPGSaveFiles: directory does not exist: {}", dirPath.toStdString());
        return {};
    }

    QStringList filters = {"file*.rpgsave", "file*.rmmzsave"};
    QStringList files;
    const auto entries = dir.entryInfoList(filters, QDir::Files, QDir::Name);
    for (const auto& entry : entries) {
        if (!entry.fileName().endsWith(".bak")) {
            files << entry.absoluteFilePath();
            LOG_DEBUG("  found save: {}", entry.absoluteFilePath().toStdString());
        }
    }
    LOG_INFO("findRPGSaveFiles({}): found {} saves", dirPath.toStdString(), files.size());
    return files;
}

QString getSaveSlotPath(const QString& saveDir, int slot)
{
    return QString("%1/file%2.rpgsave").arg(saveDir, QString::number(slot));
}

int getNextSaveSlot(const QString& saveDir)
{
    QDir dir(saveDir);
    if (!dir.exists()) return 1;

    int maxSlot = 0;
    const auto entries = dir.entryInfoList({"file*.rpgsave"}, QDir::Files, QDir::Name);
    for (const auto& entry : entries) {
        if (entry.fileName().endsWith(".bak")) continue;
        QString name = entry.fileName();
        name.remove("file");
        name.remove(".rpgsave");
        bool ok = false;
        int slot = name.toInt(&ok);
        if (ok && slot > maxSlot) maxSlot = slot;
    }
    int next = maxSlot + 1;
    LOG_DEBUG("getNextSaveSlot({}): max={}, next={}", saveDir.toStdString(), maxSlot, next);
    return next;
}

QString gameDataPathFromRoot(const QString& gameRoot)
{
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

QString saveDirFromRoot(const QString& gameRoot)
{
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

bool isRPGMakerGameRoot(const QString& gameRoot)
{
    QDir dir(gameRoot);
    const auto files = dir.entryList(QDir::Files);

    for (const auto& f : files) {
        QString lower = f.toLower();
        if (lower == "game.exe" || lower == "nw.exe")
            return true;
    }

    bool hasNwDll = false;
    for (const auto& f : files) {
        if (f.toLower() == "nw.dll") { hasNwDll = true; break; }
    }
    if (hasNwDll) {
        for (const auto& f : files) {
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
        const auto saveFiles = sd.entryList(QDir::Files);
        for (const auto& f : saveFiles) {
            for (const auto& ext : saveExtensions) {
                if (f.toLower().endsWith(ext))
                    return true;
            }
        }
    }

    return false;
}
