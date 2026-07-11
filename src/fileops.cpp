#include "mainwindow.h"
#include "helpers.h"
#include "logger.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMessageBox>

// --- Drag and Drop ---

void MainWindow::handleDrop(const QString& path)
{
    LOG_INFO("handleDrop: {}", path.toStdString());
    QFileInfo fi(path);

    QString suffix = fi.suffix().toLower();
    if (fi.isFile() && (suffix == "rpgsave" || suffix == "rmmzsave")) {
        LOG_INFO("  -> direct save file");
        m_warningLabel->hide();
        loadFromSaveFile(path);
        return;
    }

    if (fi.isDir()) {
        QString saveDir = saveDirFromRoot(path);
        QString dataDir = gameDataPathFromRoot(path);

        if (!saveDir.isEmpty()) {
            LOG_INFO("  -> game root folder (saveDir: {}, dataDir: {})",
                saveDir.toStdString(), dataDir.toStdString());
            m_gameRoot = QDir(path).absolutePath();

            if (!isRPGMakerGameRoot(m_gameRoot)) {
                LOG_WARN("  -> folder does not look like an RPG Maker game");
                m_warningLabel->setToolTip(
                    "No RPG Maker executables or save files detected.\n"
                    "Expected: Game.exe, nw.exe, a game .exe + nw.dll, or save files\n"
                    "with recognized extensions (.rpgsave, .rmmzsave, .rvdata2, etc.)");
                m_warningLabel->show();
            } else {
                m_warningLabel->hide();
            }

            loadFromGameRoot(m_gameRoot);
            return;
        }

        LOG_WARN("  -> not a game root (no save directory)");
        QMessageBox::warning(this, "Invalid Folder",
            "This folder doesn't look like an RPG Maker MV game root.\n"
            "Expected: <game>/www/save/ or <game>/save/");
        return;
    }

    LOG_WARN("  -> unrecognized path type");
}

// --- File operations ---

void MainWindow::onSave()
{
    if (!m_save.isLoaded()) {
        QMessageBox::information(this, "Nothing to Save", "No save file is loaded.");
        return;
    }
    QString savePath = m_save.filePath();
    if (savePath.isEmpty()) {
        onSaveAs();
        return;
    }
    LOG_INFO("Saving to current file: {}", savePath.toStdString());
    if (m_save.saveToFile(savePath)) {
        setStatus(QString("Saved to %1").arg(savePath));
        setTitle();
    } else {
        QMessageBox::warning(this, "Save Error", m_save.errorString());
    }
}

void MainWindow::onSaveAs()
{
    if (!m_save.isLoaded()) {
        QMessageBox::information(this, "Nothing to Save", "No save file is loaded.");
        return;
    }
    int nextSlot = getNextSaveSlot(m_saveDir);
    if (nextSlot > 99) nextSlot = 99;
    QString defaultPath = getSaveSlotPath(m_saveDir, nextSlot);
    QString path = QFileDialog::getSaveFileName(
        this, "Save As", defaultPath,
        "RPG Maker Saves (*.rpgsave *.rmmzsave);;All Files (*)");
    if (!path.isEmpty()) {
        if (m_save.saveToFile(path)) {
            setStatus(QString("Saved to %1").arg(path));
            setTitle();
        } else {
            QMessageBox::warning(this, "Save Error", m_save.errorString());
        }
    }
}

void MainWindow::onLoadGameData()
{
    QString gameRoot = QFileDialog::getExistingDirectory(
        this, "Select Game Root Folder");
    if (gameRoot.isEmpty()) return;
    m_gameRoot = QDir(gameRoot).absolutePath();
    loadGameDataFromRoot(m_gameRoot);
    if (m_save.isLoaded()) populateAllTabs();
}

void MainWindow::onLoadTranslations()
{
    QString path = QFileDialog::getOpenFileName(
        this, "Load Translation File", {},
        "Translation JSON (*.json);;All Files (*)");
    if (path.isEmpty()) return;
    if (m_save.loadTranslations(path)) {
        updateLocaleCombo();
        setStatus(QString("Translation loaded: %1").arg(path));
        if (m_save.isLoaded()) populateAllTabs();
    } else {
        QMessageBox::warning(this, "Error", "Failed to load translation file.");
    }
}

void MainWindow::onExportTranslationTemplate()
{
    if (!m_save.isLoaded()) {
        QMessageBox::information(this, "Nothing to Export",
            "Load a save file first to export variable/switch names.");
        return;
    }
    if (m_gameRoot.isEmpty()) {
        QMessageBox::information(this, "No Game Data",
            "Load game data (System.json with variable/switch names) first.");
        return;
    }
    QString path = QFileDialog::getSaveFileName(
        this, "Export Translation Template", "translation_template.json",
        "Translation JSON (*.json);;All Files (*)");
    if (path.isEmpty()) return;
    if (m_save.exportTranslationTemplate(path)) {
        setStatus(QString("Translation template exported: %1").arg(path));
    } else {
        QMessageBox::warning(this, "Export Error", m_save.errorString());
    }
}

// --- Core load logic ---

void MainWindow::loadFromGameRoot(const QString& gameRoot)
{
    LOG_INFO("loadFromGameRoot: {}", gameRoot.toStdString());

    QString saveDir = saveDirFromRoot(gameRoot);
    if (saveDir.isEmpty()) {
        LOG_ERROR("No save directory found in {}", gameRoot.toStdString());
        QMessageBox::warning(this, "Error", "No save directory found.\nExpected: www/save/ or save/");
        return;
    }

    auto saves = findRPGSaveFiles(saveDir);
    if (saves.isEmpty()) {
        LOG_ERROR("No save files found in {}", saveDir.toStdString());
        QMessageBox::warning(this, "Error", "No save files found in the save directory.");
        return;
    }

    m_saveDir = saveDir;
    m_gameRoot = gameRoot;

    QString savePath;
    if (saves.size() == 1) {
        savePath = saves.first();
    } else {
        int idx = askUserForSaveSlot(saves);
        if (idx < 0) return;
        savePath = saves[idx];
    }

    LOG_INFO("Selected save: {}", savePath.toStdString());
    loadFromSaveFile(savePath);
}

void MainWindow::loadFromSaveFile(const QString& saveFilePath)
{
    LOG_INFO("loadFromSaveFile: {}", saveFilePath.toStdString());

    if (!m_save.loadFromFile(saveFilePath)) {
        LOG_ERROR("Failed to load save: {}", m_save.errorString().toStdString());
        QMessageBox::warning(this, "Load Error", m_save.errorString());
        return;
    }

    m_saveDir = QFileInfo(saveFilePath).absolutePath();
    LOG_INFO("Save dir: {}", m_saveDir.toStdString());

    if (m_gameRoot.isEmpty()) {
        QDir d(m_saveDir);
        for (int i = 0; i < 3; ++i) {
            QString candidate = d.absolutePath();
            QString dataDir = gameDataPathFromRoot(candidate);
            if (!dataDir.isEmpty()) {
                m_gameRoot = candidate;
                LOG_INFO("Found game root by walking up: {}", m_gameRoot.toStdString());
                break;
            }
            if (!d.cdUp()) break;
        }
    }

    if (!m_gameRoot.isEmpty()) {
        loadGameDataFromRoot(m_gameRoot);
    }

    populateAllTabs();
    setTitle();
    setStatus(QString("Loaded: %1").arg(saveFilePath));
    LOG_INFO("Save loaded successfully");
}

void MainWindow::loadGameDataFromRoot(const QString& gameRoot)
{
    LOG_INFO("loadGameDataFromRoot: {}", gameRoot.toStdString());
    QString dataPath = gameDataPathFromRoot(gameRoot);
    if (dataPath.isEmpty()) {
        LOG_WARN("No data directory found in game root");
        return;
    }
    QString systemPath = dataPath + "/System.json";
    QString itemsPath = dataPath + "/Items.json";

    LOG_INFO("  System.json: {}", systemPath.toStdString());
    LOG_INFO("  Items.json:  {}", itemsPath.toStdString());
    LOG_INFO("  exists: sys={}, items={}",
        QFile::exists(systemPath), QFile::exists(itemsPath));

    m_save.loadGameData(systemPath, itemsPath);
    m_save.scanForTranslations(gameRoot);
    updateLocaleCombo();
    setStatus("Game data loaded");
}
