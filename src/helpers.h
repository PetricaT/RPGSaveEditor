#pragma once

#include <QString>
#include <QStringList>

QStringList findRPGSaveFiles(const QString& dirPath);
QString getSaveSlotPath(const QString& saveDir, int slot);
int getNextSaveSlot(const QString& saveDir);
QString gameDataPathFromRoot(const QString& gameRoot);
QString saveDirFromRoot(const QString& gameRoot);
bool isRPGMakerGameRoot(const QString& gameRoot);
