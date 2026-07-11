#include "mainwindow.h"
#include "helpers.h"
#include "logger.h"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidgetAction>

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
    m_localeCombo->addItem("Native", "native");
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

// --- Drag and Drop events ---

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

// --- Locale ---

void MainWindow::onLocaleChanged(int index)
{
    if (index < 0) return;
    QString locale = m_localeCombo->itemData(index).toString();
    LOG_INFO("Locale changed to: {}", locale.toStdString());
    m_save.setActiveLocale(locale);
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

// --- About ---

void MainWindow::onAbout()
{
    QDialog dlg(this);
    dlg.setWindowTitle("About RPGSaveEditor");

    auto* layout = new QVBoxLayout(&dlg);

    auto* label = new QLabel(
        "<p>RPGSaveEditor v1.0</p>"
        "<p>A cross-platform RPG Maker MV save file editor.</p>"
        "<p>Drag &amp; drop a game root folder or .rpgsave file.<br>"
        "Saves are written to a new slot to prevent overwriting.</p>"
        "<hr>"
        "<p>Author: <a href=\"https://github.com/PetricaT\">PetricaT</a></p>");
    label->setTextFormat(Qt::RichText);
    label->setOpenExternalLinks(true);
    label->setWordWrap(true);
    layout->addWidget(label);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok);
    layout->addWidget(buttonBox);
    QObject::connect(buttonBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);

    dlg.setMinimumWidth(400);
    dlg.exec();
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
