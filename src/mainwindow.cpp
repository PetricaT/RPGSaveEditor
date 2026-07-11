#include "mainwindow.h"
#include "logger.h"

#include <rapidfuzz/fuzz.hpp>

#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QSplitter>
#include <QDir>

namespace {

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

} // anonymous namespace
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <functional>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    LOG_INFO("MainWindow constructor");
    setAcceptDrops(true);
    setWindowIcon(QIcon(":/appicon.png"));
    setupUI();
    setupMenuBar();
    setupStatusBar();
    setTitle();
    LOG_INFO("MainWindow ready");
}

void MainWindow::setupUI()
{
    m_stacked = new QStackedWidget(this);
    setCentralWidget(m_stacked);

    m_welcomeWidget = new QWidget;
    auto* wl = new QVBoxLayout(m_welcomeWidget);
    wl->setAlignment(Qt::AlignCenter);
    auto* label = new QLabel("Drag & drop a game folder or .rpgsave file here");
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet("font-size: 18px; color: #888;");
    auto* sub = new QLabel("Or use File > Open to browse");
    sub->setAlignment(Qt::AlignCenter);
    sub->setStyleSheet("font-size: 14px; color: #666;");
    wl->addWidget(label);
    wl->addWidget(sub);
    m_stacked->addWidget(m_welcomeWidget);

    m_tabs = new QTabWidget;
    setupTabs();
    m_stacked->addWidget(m_tabs);

    m_stacked->setCurrentWidget(m_welcomeWidget);
    resize(1000, 700);
}

void MainWindow::setupTabs()
{
    // --- Variables Tab ---
    auto* varTab = new QWidget;
    auto* varLayout = new QVBoxLayout(varTab);

    m_varFilter = new QLineEdit;
    m_varFilter->setPlaceholderText("Filter variables...");
    m_varFilter->setClearButtonEnabled(true);
    varLayout->addWidget(m_varFilter);

    m_varTable = new QTableWidget;
    m_varTable->setColumnCount(3);
    m_varTable->setHorizontalHeaderLabels({"ID", "Name", "Value"});
    m_varTable->horizontalHeader()->setStretchLastSection(true);
    m_varTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_varTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_varTable->setSelectionBehavior(QTableWidget::SelectRows);
    m_varTable->setAlternatingRowColors(true);
    m_varTable->verticalHeader()->setVisible(false);
    varLayout->addWidget(m_varTable);

    connect(m_varFilter, &QLineEdit::textChanged, this, [this](const QString& text) {
        for (int i = 0; i < m_varTable->rowCount(); ++i) {
            bool match = text.isEmpty();
            if (!match) {
                for (int c = 0; c < m_varTable->columnCount(); ++c) {
                    auto* item = m_varTable->item(i, c);
                    if (item && item->text().contains(text, Qt::CaseInsensitive)) {
                        match = true;
                        break;
                    }
                }
            }
            m_varTable->setRowHidden(i, !match);
        }
    });

    connect(m_varTable, &QTableWidget::cellChanged, this, [this](int row, int column) {
        if (column != 2) return;
        auto* item = m_varTable->item(row, column);
        if (!item) return;
        int id = m_varTable->item(row, 0)->text().toInt();
        QString name = m_varTable->item(row, 1) ? m_varTable->item(row, 1)->text() : QString();
        QString valStr = item->text();

        // Read old value from save before overwriting
        json oldVal = m_save.getVariable(id);
        QString oldStr;
        if (oldVal.is_null()) oldStr = "null";
        else if (oldVal.is_boolean()) oldStr = oldVal.get<bool>() ? "true" : "false";
        else if (oldVal.is_number_integer()) oldStr = QString::number(oldVal.get<int64_t>());
        else if (oldVal.is_number_float()) oldStr = QString::number(oldVal.get<double>(), 'g', 6);
        else if (oldVal.is_string()) oldStr = QString::fromStdString(oldVal.get<std::string>());
        else oldStr = "...";

        LOG_INFO("Editing variable with name: \"{}\", value: \"{}\", new value: \"{}\"", name.toStdString(), oldStr.toStdString(), valStr.toStdString());
        bool ok = false;
        int64_t intVal = valStr.toLongLong(&ok);
        if (ok) { m_save.setVariable(id, intVal); return; }
        double dblVal = valStr.toDouble(&ok);
        if (ok) { m_save.setVariable(id, dblVal); return; }
        if (valStr.toLower() == "true") { m_save.setVariable(id, true); return; }
        if (valStr.toLower() == "false") { m_save.setVariable(id, false); return; }
        if (valStr.toLower() == "null") { m_save.setVariable(id, nullptr); return; }
        m_save.setVariable(id, valStr.toStdString());
    });

    m_tabs->addTab(varTab, "Variables");

    // --- Switches Tab ---
    auto* swTab = new QWidget;
    auto* swLayout = new QVBoxLayout(swTab);

    m_swFilter = new QLineEdit;
    m_swFilter->setPlaceholderText("Filter switches...");
    m_swFilter->setClearButtonEnabled(true);
    swLayout->addWidget(m_swFilter);

    m_swTable = new QTableWidget;
    m_swTable->setColumnCount(3);
    m_swTable->setHorizontalHeaderLabels({"ID", "Name", "Enabled"});
    m_swTable->horizontalHeader()->setStretchLastSection(true);
    m_swTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_swTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_swTable->setSelectionBehavior(QTableWidget::SelectRows);
    m_swTable->setAlternatingRowColors(true);
    m_swTable->verticalHeader()->setVisible(false);
    swLayout->addWidget(m_swTable);

    connect(m_swFilter, &QLineEdit::textChanged, this, [this](const QString& text) {
        for (int i = 0; i < m_swTable->rowCount(); ++i) {
            bool match = text.isEmpty();
            if (!match) {
                for (int c = 0; c < m_swTable->columnCount(); ++c) {
                    auto* item = m_swTable->item(i, c);
                    if (item && item->text().contains(text, Qt::CaseInsensitive)) {
                        match = true;
                        break;
                    }
                }
            }
            m_swTable->setRowHidden(i, !match);
        }
    });

    connect(m_swTable, &QTableWidget::cellChanged, this, [this](int row, int column) {
        if (column != 2) return;
        auto* item = m_swTable->item(row, column);
        if (!item) return;
        int id = m_swTable->item(row, 0)->text().toInt();
        QString name = m_swTable->item(row, 1) ? m_swTable->item(row, 1)->text() : QString();
        bool newVal = item->text().toLower() == "true" || item->text() == "1";
        bool oldVal = m_save.getSwitch(id);
        LOG_INFO("Editing switch with name: \"{}\", value: \"{}\", new value: \"{}\"", name.toStdString(), oldVal ? "true" : "false", newVal ? "true" : "false");
        m_save.setSwitch(id, newVal);
    });

    m_tabs->addTab(swTab, "Switches");

    // --- Party Tab ---
    auto* partyTab = new QWidget;
    auto* partyLayout = new QVBoxLayout(partyTab);

    auto* goldLayout = new QHBoxLayout;
    goldLayout->addWidget(new QLabel("Gold:"));
    auto* goldEdit = new QLineEdit;
    goldEdit->setObjectName("goldEdit");
    goldLayout->addWidget(goldEdit);
    goldLayout->addStretch();
    goldLayout->addWidget(new QLabel("Steps:"));
    auto* stepsEdit = new QLineEdit;
    stepsEdit->setObjectName("stepsEdit");
    goldLayout->addWidget(stepsEdit);
    goldLayout->addStretch();
    partyLayout->addLayout(goldLayout);

    connect(goldEdit, &QLineEdit::editingFinished, this, [this, goldEdit]() {
        m_save.setGold(goldEdit->text().toInt());
    });
    connect(stepsEdit, &QLineEdit::editingFinished, this, [this, stepsEdit]() {
        m_save.setSteps(stepsEdit->text().toInt());
    });

    auto* partySplitter = new QSplitter(Qt::Vertical);

    m_itemTable = new QTableWidget;
    m_itemTable->setColumnCount(3);
    m_itemTable->setHorizontalHeaderLabels({"ID", "Name", "Quantity"});
    m_itemTable->horizontalHeader()->setStretchLastSection(true);
    m_itemTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_itemTable->setAlternatingRowColors(true);
    m_itemTable->verticalHeader()->setVisible(false);
    auto* iw = new QWidget;
    auto* il = new QVBoxLayout(iw);
    il->setContentsMargins(0, 0, 0, 0);
    il->addWidget(new QLabel("Items"));
    il->addWidget(m_itemTable);
    partySplitter->addWidget(iw);

    m_weaponTable = new QTableWidget;
    m_weaponTable->setColumnCount(3);
    m_weaponTable->setHorizontalHeaderLabels({"ID", "Name", "Quantity"});
    m_weaponTable->horizontalHeader()->setStretchLastSection(true);
    m_weaponTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_weaponTable->setAlternatingRowColors(true);
    m_weaponTable->verticalHeader()->setVisible(false);
    auto* ww = new QWidget;
    auto* wl = new QVBoxLayout(ww);
    wl->setContentsMargins(0, 0, 0, 0);
    wl->addWidget(new QLabel("Weapons"));
    wl->addWidget(m_weaponTable);
    partySplitter->addWidget(ww);

    m_armorTable = new QTableWidget;
    m_armorTable->setColumnCount(3);
    m_armorTable->setHorizontalHeaderLabels({"ID", "Name", "Quantity"});
    m_armorTable->horizontalHeader()->setStretchLastSection(true);
    m_armorTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_armorTable->setAlternatingRowColors(true);
    m_armorTable->verticalHeader()->setVisible(false);
    auto* aw = new QWidget;
    auto* al = new QVBoxLayout(aw);
    al->setContentsMargins(0, 0, 0, 0);
    al->addWidget(new QLabel("Armors"));
    al->addWidget(m_armorTable);
    partySplitter->addWidget(aw);

    auto* partyFilterLayout = new QHBoxLayout;
    partyFilterLayout->addWidget(new QLabel("Search:"));
    m_partyFilter = new QLineEdit;
    m_partyFilter->setPlaceholderText("Fuzzy search items, weapons, armors...");
    partyFilterLayout->addWidget(m_partyFilter);
    partyLayout->addLayout(partyFilterLayout);

    connect(m_partyFilter, &QLineEdit::textChanged, this, &MainWindow::applyPartyFilter);

    partyLayout->addWidget(partySplitter);

    auto applyMultiQuantity = [](QTableWidget* table, int triggeredRow, int column, int val) {
        QSet<int> applied;
        applied.insert(triggeredRow);
        auto selItems = table->selectedItems();
        if (selItems.size() <= 1) return;
        table->blockSignals(true);
        for (auto* selItem : selItems) {
            if (selItem->column() == column && !applied.contains(selItem->row())) {
                applied.insert(selItem->row());
                selItem->setText(QString::number(val));
            }
        }
        table->blockSignals(false);
    };

    auto saveSelItems = [this](QTableWidget* table, const std::function<void(int,int)>& saveFn) {
        for (auto* selItem : table->selectedItems()) {
            if (selItem->column() == 2) {
                int id = table->item(selItem->row(), 0)->text().toInt();
                saveFn(id, selItem->text().toInt());
            }
        }
    };

    connect(m_itemTable, &QTableWidget::cellChanged, this, [this, applyMultiQuantity, saveSelItems](int row, int column) {
        if (column != 2) return;
        auto* item = m_itemTable->item(row, column);
        if (!item) return;
        int val = item->text().toInt();
        applyMultiQuantity(m_itemTable, row, column, val);
        saveSelItems(m_itemTable, [this](int id, int qty) { m_save.setItem(id, qty); });
    });
    connect(m_weaponTable, &QTableWidget::cellChanged, this, [this, applyMultiQuantity, saveSelItems](int row, int column) {
        if (column != 2) return;
        auto* item = m_weaponTable->item(row, column);
        if (!item) return;
        int val = item->text().toInt();
        applyMultiQuantity(m_weaponTable, row, column, val);
        saveSelItems(m_weaponTable, [this](int id, int qty) { m_save.setWeapon(id, qty); });
    });
    connect(m_armorTable, &QTableWidget::cellChanged, this, [this, applyMultiQuantity, saveSelItems](int row, int column) {
        if (column != 2) return;
        auto* item = m_armorTable->item(row, column);
        if (!item) return;
        int val = item->text().toInt();
        applyMultiQuantity(m_armorTable, row, column, val);
        saveSelItems(m_armorTable, [this](int id, int qty) { m_save.setArmor(id, qty); });
    });

    m_tabs->addTab(partyTab, "Party");

    // --- Actors Tab ---
    m_actorTable = new QTableWidget;
    m_actorTable->setColumnCount(9);
    m_actorTable->setHorizontalHeaderLabels({"ID", "Name", "HP", "Max HP", "MP", "Max MP", "TP", "Level", "EXP"});
    m_actorTable->horizontalHeader()->setStretchLastSection(true);
    m_actorTable->setAlternatingRowColors(true);
    m_actorTable->verticalHeader()->setVisible(false);
    m_tabs->addTab(m_actorTable, "Actors");

    connect(m_actorTable, &QTableWidget::cellChanged, this, [this](int row, int column) {
        if (column <= 1 || column == 8) return; // ID/Name/EXP are read-only
        int id = m_actorTable->item(row, 0)->text().toInt();
        json actor = m_save.getActor(id);
        if (actor.is_null()) return;
        auto* item = m_actorTable->item(row, column);
        if (!item) return;
        int val = item->text().toInt();
        switch (column) {
            case 2: actor["_hp"] = val; break;
            case 3: actor["_maxHp"] = val; break;
            case 4: actor["_mp"] = val; break;
            case 5: actor["_maxMp"] = val; break;
            case 6: actor["_tp"] = val; break;
            case 7: actor["_level"] = val; break;
        }
        m_save.setActor(id, actor);
    });

    // --- JSON Tree Tab ---
    m_jsonTree = new QTreeWidget;
    m_jsonTree->setHeaderLabels({"Key", "Value"});
    m_jsonTree->setAlternatingRowColors(true);
    m_tabs->addTab(m_jsonTree, "JSON Tree");
}

void MainWindow::setupMenuBar()
{
    auto* fileMenu = menuBar()->addMenu("&File");

    auto* openAction = fileMenu->addAction("&Open...");
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, [this]() {
        QString path = QFileDialog::getOpenFileName(
            this, "Open RPG Maker Save File", {},
            "RPG Maker Saves (*.rpgsave *.rmmzsave);;All Files (*)");
        if (!path.isEmpty()) handleDrop(path);
    });

    fileMenu->addSeparator();

    auto* saveAction = fileMenu->addAction("&Save");
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, &MainWindow::onSave);

    auto* saveAsAction = fileMenu->addAction("Save &As...");
    saveAsAction->setShortcut(QKeySequence("Ctrl+Shift+S"));
    connect(saveAsAction, &QAction::triggered, this, &MainWindow::onSaveAs);

    fileMenu->addSeparator();

    auto* loadGameDataAction = fileMenu->addAction("Load Game &Data...");
    connect(loadGameDataAction, &QAction::triggered, this, &MainWindow::onLoadGameData);

    auto* loadTransAction = fileMenu->addAction("Load &Translations...");
    connect(loadTransAction, &QAction::triggered, this, &MainWindow::onLoadTranslations);

    auto* exportTransTemplateAction = fileMenu->addAction("Export Translation &Template...");
    connect(exportTransTemplateAction, &QAction::triggered, this, &MainWindow::onExportTranslationTemplate);

    fileMenu->addSeparator();

    auto* quitAction = fileMenu->addAction("&Quit");
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);

    auto* viewMenu = menuBar()->addMenu("&View");

    auto* localeLabel = new QAction("Locale:", this);
    localeLabel->setDisabled(true);
    viewMenu->addAction(localeLabel);

    m_localeCombo = new QComboBox;
    m_localeCombo->addItem("Native (Japanese)", "native");
    auto* localeWidget = new QWidgetAction(this);
    localeWidget->setDefaultWidget(m_localeCombo);
    viewMenu->addAction(localeWidget);

    connect(m_localeCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onLocaleChanged);
    connect(&m_save, &RPGSave::localeChanged, this, [this](const QString&) {
        if (m_save.isLoaded()) populateAllTabs();
    });

    auto* helpMenu = menuBar()->addMenu("&Help");
    auto* aboutAction = helpMenu->addAction("&About");
    connect(aboutAction, &QAction::triggered, this, &MainWindow::onAbout);
}

void MainWindow::setupStatusBar()
{
    m_statusLabel = new QLabel("Ready");
    statusBar()->addWidget(m_statusLabel);

    m_warningLabel = new QLabel("\u26A0");
    m_warningLabel->setStyleSheet("color: #e6a817; font-size: 16px; font-weight: bold;");
    m_warningLabel->setToolTip("This folder may not be a valid RPG Maker game distribution");
    m_warningLabel->setCursor(Qt::ArrowCursor);
    m_warningLabel->hide();
    statusBar()->addPermanentWidget(m_warningLabel);
}

// --- Drag and Drop ---

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event)
{
    const auto urls = event->mimeData()->urls();
    if (urls.isEmpty()) return;
    handleDrop(urls.first().toLocalFile());
}

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

void MainWindow::onLocaleChanged(int index)
{
    if (index < 0) return;
    QString locale = m_localeCombo->itemData(index).toString();
    LOG_INFO("Locale changed to: {}", locale.toStdString());
    m_save.setActiveLocale(locale);
}

void MainWindow::onAbout()
{
    QMessageBox box(this);
    box.setWindowTitle("About RPGSaveEditor");
    box.setTextFormat(Qt::RichText);
    box.setText(
        "<p>RPGSaveEditor v1.0</p>"
        "<p>A cross-platform RPG Maker MV save file editor.</p>"
        "<p>Drag &amp; drop a game root folder or .rpgsave file.<br>"
        "Saves are written to a new slot to prevent overwriting.</p>"
        "<hr>"
        "<p>Author: <a href=\"https://github.com/PetricaT\">PetricaT</a></p>");
    box.setStandardButtons(QMessageBox::Ok);
    box.exec();
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

    // If we don't have a game root yet, try to find it by walking up
    if (m_gameRoot.isEmpty()) {
        QDir d(m_saveDir);
        // www/save/ -> go up to find game root
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

    // Load game data (variable names, item names)
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

// --- Tab population ---

void MainWindow::populateAllTabs()
{
    LOG_DEBUG("populateAllTabs");
    populateVariablesTab();
    populateSwitchesTab();
    populatePartyTab();
    populateActorsTab();
    refreshJsonTree();
    m_stacked->setCurrentWidget(m_tabs);
}

void MainWindow::populateVariablesTab()
{
    LOG_DEBUG("populateVariablesTab");
    m_varTable->blockSignals(true);
    m_varTable->setRowCount(0);

    int count = m_save.variableCount();
    for (int i = 0; i < count; ++i) {
        json val = m_save.getVariable(i);
        QString name = m_save.variableName(i);
        bool hasName = !name.startsWith("Var ");
        if (!hasName && (val.is_null() || (val.is_number() && val.get<double>() == 0)))
            continue;

        int row = m_varTable->rowCount();
        m_varTable->insertRow(row);

        auto* idItem = new QTableWidgetItem(QString::number(i));
        idItem->setFlags(idItem->flags() & ~Qt::ItemIsEditable);
        m_varTable->setItem(row, 0, idItem);

        auto* nameItem = new QTableWidgetItem(name);
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        if (hasName) nameItem->setForeground(QColor(100, 180, 255));
        m_varTable->setItem(row, 1, nameItem);

        QString valStr;
        if (val.is_null()) valStr = "null";
        else if (val.is_boolean()) valStr = val.get<bool>() ? "true" : "false";
        else if (val.is_number_integer()) valStr = QString::number(val.get<int64_t>());
        else if (val.is_number_float()) valStr = QString::number(val.get<double>(), 'g', 6);
        else if (val.is_string()) valStr = QString::fromStdString(val.get<std::string>());
        else valStr = "...";

        m_varTable->setItem(row, 2, new QTableWidgetItem(valStr));
    }
    m_varTable->resizeColumnsToContents();
    m_varTable->blockSignals(false);
}

void MainWindow::populateSwitchesTab()
{
    LOG_DEBUG("populateSwitchesTab");
    m_swTable->blockSignals(true);
    m_swTable->setRowCount(0);

    int count = m_save.switchCount();
    for (int i = 0; i < count; ++i) {
        bool val = m_save.getSwitch(i);
        QString name = m_save.switchName(i);
        bool hasName = !name.startsWith("Switch ");
        if (!hasName && !val) continue;

        int row = m_swTable->rowCount();
        m_swTable->insertRow(row);

        auto* idItem = new QTableWidgetItem(QString::number(i));
        idItem->setFlags(idItem->flags() & ~Qt::ItemIsEditable);
        m_swTable->setItem(row, 0, idItem);

        auto* nameItem = new QTableWidgetItem(name);
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        if (hasName) nameItem->setForeground(QColor(100, 180, 255));
        m_swTable->setItem(row, 1, nameItem);

        m_swTable->setItem(row, 2, new QTableWidgetItem(val ? "true" : "false"));
    }
    m_swTable->resizeColumnsToContents();
    m_swTable->blockSignals(false);
}

void MainWindow::populatePartyTab()
{
    LOG_DEBUG("populatePartyTab");
    auto* goldEdit = findChild<QLineEdit*>("goldEdit");
    auto* stepsEdit = findChild<QLineEdit*>("stepsEdit");
    if (goldEdit) goldEdit->setText(QString::number(m_save.gold()));
    if (stepsEdit) stepsEdit->setText(QString::number(m_save.steps()));

    m_itemTable->blockSignals(true);
    m_itemTable->setRowCount(0);
    auto items = m_save.items();
    for (auto it = items.constBegin(); it != items.constEnd(); ++it) {
        int row = m_itemTable->rowCount();
        m_itemTable->insertRow(row);
        auto* idItem = new QTableWidgetItem(QString::number(it.key()));
        idItem->setFlags(idItem->flags() & ~Qt::ItemIsEditable);
        m_itemTable->setItem(row, 0, idItem);
        auto* nameItem = new QTableWidgetItem(m_save.itemName(it.key()));
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        m_itemTable->setItem(row, 1, nameItem);
        m_itemTable->setItem(row, 2, new QTableWidgetItem(QString::number(it.value())));
    }
    m_itemTable->resizeColumnsToContents();
    m_itemTable->blockSignals(false);

    m_weaponTable->blockSignals(true);
    m_weaponTable->setRowCount(0);
    auto weapons = m_save.weapons();
    for (auto it = weapons.constBegin(); it != weapons.constEnd(); ++it) {
        int row = m_weaponTable->rowCount();
        m_weaponTable->insertRow(row);
        auto* idItem = new QTableWidgetItem(QString::number(it.key()));
        idItem->setFlags(idItem->flags() & ~Qt::ItemIsEditable);
        m_weaponTable->setItem(row, 0, idItem);
        auto* nameItem = new QTableWidgetItem(m_save.weaponName(it.key()));
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        m_weaponTable->setItem(row, 1, nameItem);
        m_weaponTable->setItem(row, 2, new QTableWidgetItem(QString::number(it.value())));
    }
    m_weaponTable->resizeColumnsToContents();
    m_weaponTable->blockSignals(false);

    m_armorTable->blockSignals(true);
    m_armorTable->setRowCount(0);
    auto armors = m_save.armors();
    for (auto it = armors.constBegin(); it != armors.constEnd(); ++it) {
        int row = m_armorTable->rowCount();
        m_armorTable->insertRow(row);
        auto* idItem = new QTableWidgetItem(QString::number(it.key()));
        idItem->setFlags(idItem->flags() & ~Qt::ItemIsEditable);
        m_armorTable->setItem(row, 0, idItem);
        auto* nameItem = new QTableWidgetItem(m_save.armorName(it.key()));
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        m_armorTable->setItem(row, 1, nameItem);
        m_armorTable->setItem(row, 2, new QTableWidgetItem(QString::number(it.value())));
    }
    m_armorTable->resizeColumnsToContents();
    m_armorTable->blockSignals(false);
}

void MainWindow::populateActorsTab()
{
    LOG_DEBUG("populateActorsTab");
    m_actorTable->blockSignals(true);
    m_actorTable->setRowCount(0);

    auto safeNum = [](const json& obj, const char* key, int fallback) -> int {
        try {
            if (!obj.contains(key)) return fallback;
            const auto& val = obj[key];
            if (val.is_number_integer()) return val.get<int>();
            if (val.is_number_unsigned()) return static_cast<int>(val.get<uint64_t>());
            if (val.is_number_float()) return static_cast<int>(val.get<double>());
            return fallback;
        } catch (...) { return fallback; }
    };

    auto safeExp = [](const json& obj, int fallback) -> int {
        try {
            if (!obj.contains("_exp")) return fallback;
            const auto& exp = obj["_exp"];
            if (exp.is_number()) return exp.get<int>();
            if (exp.is_object() && exp.contains("1"))
                return exp["1"].is_number() ? exp["1"].get<int>() : fallback;
            return fallback;
        } catch (...) { return fallback; }
    };

    int maxId = m_save.maxActorId();
    if (maxId <= 0) {
        // No game data loaded, fall back to save entries only
        for (int id = 1; id < 100; ++id) {
            json actor = m_save.getActor(id);
            if (actor.is_null() || !actor.is_object()) continue;
            int row = m_actorTable->rowCount();
            m_actorTable->insertRow(row);
            auto addCell = [&](int col, const QVariant& val, bool editable = false) {
                auto* item = new QTableWidgetItem(val.toString());
                if (!editable) item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                m_actorTable->setItem(row, col, item);
            };
            addCell(0, id);
            addCell(1, m_save.actorName(id));
            addCell(2, safeNum(actor, "_hp", 0), true);
            addCell(3, safeNum(actor, "_maxHp", 0), true);
            addCell(4, safeNum(actor, "_mp", 0), true);
            addCell(5, safeNum(actor, "_maxMp", 0), true);
            addCell(6, safeNum(actor, "_tp", 0), true);
            addCell(7, safeNum(actor, "_level", 0), true);
            addCell(8, safeExp(actor, 0));
        }
    } else {
        // Show all actors from game data
        for (int id = 1; id <= maxId; ++id) {
            if (!m_save.actorName(id).startsWith("Actor ")) {
                json actor = m_save.getActor(id);
                int row = m_actorTable->rowCount();
                m_actorTable->insertRow(row);
                auto addCell = [&](int col, const QVariant& val, bool editable = false) {
                    auto* item = new QTableWidgetItem(val.toString());
                    if (!editable) item->setFlags(item->flags() & ~Qt::ItemIsEditable);
                    m_actorTable->setItem(row, col, item);
                };
                bool hasSaveData = actor.is_object() && !actor.is_null();
                addCell(0, id);
                addCell(1, m_save.actorName(id));
                addCell(2, hasSaveData ? safeNum(actor, "_hp", 0) : 0, true);
                addCell(3, hasSaveData ? safeNum(actor, "_maxHp", 0) : 0, true);
                addCell(4, hasSaveData ? safeNum(actor, "_mp", 0) : 0, true);
                addCell(5, hasSaveData ? safeNum(actor, "_maxMp", 0) : 0, true);
                addCell(6, hasSaveData ? safeNum(actor, "_tp", 0) : 0, true);
                addCell(7, hasSaveData ? safeNum(actor, "_level", 0) : 0, true);
                addCell(8, hasSaveData ? safeExp(actor, 0) : 0);
            }
        }
    }
    m_actorTable->resizeColumnsToContents();
    m_actorTable->blockSignals(false);
}

void MainWindow::refreshJsonTree()
{
    LOG_DEBUG("refreshJsonTree");
    m_jsonTree->clear();
    if (!m_save.isLoaded()) return;

    const json& root = m_save.root();
    std::function<void(QTreeWidgetItem*, const QString&, const json&)> addJsonNode;
    addJsonNode = [&](QTreeWidgetItem* parent, const QString& key, const json& val) {
        auto* item = new QTreeWidgetItem(parent);
        item->setText(0, key);

        if (val.is_object()) {
            item->setText(1, QString("{ %1 items }").arg(val.size()));
            for (auto it = val.begin(); it != val.end(); ++it)
                addJsonNode(item, QString::fromStdString(it.key()), *it);
        } else if (val.is_array()) {
            item->setText(1, QString("[ %1 items ]").arg(val.size()));
            for (size_t i = 0; i < val.size(); ++i)
                addJsonNode(item, QString("[%1]").arg(i), val[i]);
        } else if (val.is_string()) {
            item->setText(1, QString::fromStdString(val.get<std::string>()));
        } else if (val.is_number_integer()) {
            item->setText(1, QString::number(val.get<int64_t>()));
        } else if (val.is_number_float()) {
            item->setText(1, QString::number(val.get<double>(), 'g', 6));
        } else if (val.is_boolean()) {
            item->setText(1, val.get<bool>() ? "true" : "false");
        } else if (val.is_null()) {
            item->setText(1, "null");
        }
    };

    for (auto it = root.begin(); it != root.end(); ++it)
        addJsonNode(m_jsonTree->invisibleRootItem(), QString::fromStdString(it.key()), *it);
}

// --- Helpers ---

int MainWindow::askUserForSaveSlot(const QStringList& saves)
{
    QStringList items;
    for (const auto& s : saves) {
        items << QFileInfo(s).fileName();
    }
    bool ok = false;
    QString item = QInputDialog::getItem(this, "Select Save Slot",
        "Multiple save files found. Which one to load?",
        items, 0, false, &ok);
    if (!ok || item.isEmpty()) return -1;
    return saves.indexOf(QDir(m_saveDir).absoluteFilePath(item));
}

void MainWindow::updateLocaleCombo()
{
    m_localeCombo->blockSignals(true);
    m_localeCombo->clear();

    QStringList locales = m_save.availableLocales();
    int selectIdx = 0;
    for (int i = 0; i < locales.size(); ++i) {
        QString display = locales[i];
        if (display == "native") display = "Native (Japanese)";
        m_localeCombo->addItem(display, locales[i]);
        if (locales[i] == m_save.activeLocale()) selectIdx = i;
    }
    m_localeCombo->setCurrentIndex(selectIdx);
    m_localeCombo->blockSignals(false);
}

void MainWindow::setTitle()
{
    if (m_save.isLoaded()) {
        setWindowTitle(QString("RPGSaveEditor - %1").arg(m_save.filePath()));
    } else {
        setWindowTitle("RPGSaveEditor");
    }
}

void MainWindow::setStatus(const QString& msg)
{
    LOG_INFO("Status: {}", msg.toStdString());
    m_statusLabel->setText(msg);
}

void MainWindow::applyPartyFilter()
{
    QString query = m_partyFilter->text().trimmed();
    if (query.isEmpty()) {
        for (auto* t : {m_itemTable, m_weaponTable, m_armorTable}) {
            for (int r = 0; r < t->rowCount(); ++r)
                t->setRowHidden(r, false);
        }
        return;
    }

    std::string qStd = query.toLower().toStdString();
    for (auto* t : {m_itemTable, m_weaponTable, m_armorTable}) {
        for (int r = 0; r < t->rowCount(); ++r) {
            auto* nameItem = t->item(r, 1);
            if (!nameItem) { t->setRowHidden(r, true); continue; }
            std::string name = nameItem->text().toLower().toStdString();
            double score = rapidfuzz::fuzz::partial_ratio(qStd, name);
            t->setRowHidden(r, score < 60.0);
        }
    }
}
