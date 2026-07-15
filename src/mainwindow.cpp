#include "mainwindow.h"
#include "helpers.h"
#include "logger.h"

#include <QApplication>
#include <QCloseEvent>
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
#include <QFont>
#include <QLabel>
#include <QMenu>
#include <QPalette>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QSlider>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidgetAction>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent){
    LOG_INFO("MainWindow constructor");
    setAcceptDrops(true);
    setWindowIcon(QIcon(":/appicon.png"));
    connect(&m_save, &RPGSave::modified, this, [this]() { m_bDirty = true; setTitle(); });
    setupUI();
    setupMenuBar();
    setupSettings();
    setupStatusBar();
    setTitle();
    LOG_INFO("MainWindow ready");
}

void MainWindow::setupUI(){
    m_stacked = new QStackedWidget(this);
    setCentralWidget(m_stacked);

    m_welcomeWidget = new QWidget;
    auto* aWl = new QVBoxLayout(m_welcomeWidget);
    aWl->setAlignment(Qt::AlignCenter);
    // Title
    auto* aLabel = new QLabel("Drag & drop a game folder or RPGMaker save file here");
    aLabel->setAlignment(Qt::AlignCenter);
    QFont lf = aLabel->font();
    lf.setPointSize(18);
    aLabel->setFont(lf);
    aLabel->setForegroundRole(QPalette::Text);
    // Subtitle
    auto* aSubTitle = new QLabel("Or use File > Open to browse");
    aSubTitle->setAlignment(Qt::AlignCenter);
    QFont subTitleFont = aSubTitle->font();
    subTitleFont.setPointSize(14);
    aSubTitle->setFont(subTitleFont);
    aSubTitle->setForegroundRole(QPalette::PlaceholderText);
    // Supported file formats
    auto* aFileFormats = new QLabel("Supported file formats: .rpgsave .rmmzsave");
    aFileFormats->setAlignment(Qt::AlignCenter);
    QFont fileFormatFont = aFileFormats->font();
    fileFormatFont.setPointSize(12);
    aFileFormats->setFont(fileFormatFont);
    aFileFormats->setForegroundRole(QPalette::PlaceholderText);
    // Finishing touches
    aWl->addWidget(aLabel);
    aWl->addWidget(aSubTitle);
    aWl->addWidget(aFileFormats);

    m_stacked->addWidget(m_welcomeWidget);

    m_tabs = new QTabWidget;
    setupTabs();
    m_stacked->addWidget(m_tabs);

    m_stacked->setCurrentWidget(m_welcomeWidget);
    resize(1000, 700);
}

void MainWindow::setupMenuBar(){
    auto* aFileMenu = menuBar()->addMenu("&File");

    auto* aOpenAction = aFileMenu->addAction("&Open...");
    aOpenAction->setShortcut(QKeySequence::Open);
    connect(aOpenAction, &QAction::triggered, this, [this]() {
        QString path = QFileDialog::getOpenFileName(
            this, "Open RPG Maker Save File", {},
            "RPG Maker Saves (*.rpgsave *.rmmzsave);;All Files (*)");
        if (!path.isEmpty()) handleDrop(path);
    });

    aFileMenu->addSeparator();

    auto* aSaveAction = aFileMenu->addAction("&Save");
    aSaveAction->setShortcut(QKeySequence::Save);
    connect(aSaveAction, &QAction::triggered, this, &MainWindow::onSave);

    auto* aSaveAsAction = aFileMenu->addAction("Save &As...");
    aSaveAsAction->setShortcut(QKeySequence("Ctrl+Shift+S"));
    connect(aSaveAsAction, &QAction::triggered, this, &MainWindow::onSaveAs);

    auto* aCloseAction = aFileMenu->addAction("&Close");
    aCloseAction->setShortcut(QKeySequence::Close);
    connect(aCloseAction, &QAction::triggered, this, &MainWindow::onClose);

    aFileMenu->addSeparator();

    auto* aLoadGameDataAction = aFileMenu->addAction("Load Game &Data...");
    connect(aLoadGameDataAction, &QAction::triggered, this, &MainWindow::onLoadGameData);

    auto* aLoadTransAction = aFileMenu->addAction("Load &Translations...");
    connect(aLoadTransAction, &QAction::triggered, this, &MainWindow::onLoadTranslations);

    auto* aExportTransTemplateAction = aFileMenu->addAction("Export Translation &Template...");
    connect(aExportTransTemplateAction, &QAction::triggered, this, &MainWindow::onExportTranslationTemplate);

    aFileMenu->addSeparator();

    auto* aQuitAction = aFileMenu->addAction("&Quit");
    aQuitAction->setShortcut(QKeySequence::Quit);
    connect(aQuitAction, &QAction::triggered, qApp, &QApplication::quit);

    auto* aViewMenu = menuBar()->addMenu("&View");

    auto* aLocaleLabel = new QAction("Locale:", this);
    aLocaleLabel->setDisabled(true);
    aViewMenu->addAction(aLocaleLabel);

    m_localeCombo = new QComboBox;
    m_localeCombo->addItem("Native", "native");
    auto* aLocaleWidget = new QWidgetAction(this);
    aLocaleWidget->setDefaultWidget(m_localeCombo);
    aViewMenu->addAction(aLocaleWidget);

    connect(m_localeCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onLocaleChanged);
    connect(&m_save, &RPGSave::localeChanged, this, [this](const QString&) {
        if (m_save.isLoaded()) populateAllTabs();
    });

    auto* aHelpMenu = menuBar()->addMenu("&Help");
    auto* aAboutAction = aHelpMenu->addAction("&About");
    connect(aAboutAction, &QAction::triggered, this, &MainWindow::onAbout);
}

void MainWindow::setupSettings(){
    auto* aSettingsMenu = menuBar()->addMenu("&Settings");

    auto* aFuzzyLabel = new QAction("Fuzzy Search Threshold:", this);
    aFuzzyLabel->setDisabled(true);
    aSettingsMenu->addAction(aFuzzyLabel);

    m_fuzzySlider = new QSlider(Qt::Horizontal);
    m_fuzzySlider->setRange(0, 100);
    m_fuzzySlider->setValue(static_cast<int>(m_fuzzyThreshold));
    m_fuzzySlider->setTickPosition(QSlider::TicksBelow);
    m_fuzzySlider->setTickInterval(10);
    m_fuzzySlider->setFixedWidth(200);

    m_fuzzyValueLabel = new QLabel(QString::number(m_fuzzyThreshold, 'f', 0) + "%");

    auto* aSliderWidget = new QWidget;
    auto* aSliderLayout = new QHBoxLayout(aSliderWidget);
    aSliderLayout->setContentsMargins(8, 2, 8, 2);
    aSliderLayout->addWidget(m_fuzzySlider);
    aSliderLayout->addWidget(m_fuzzyValueLabel);

    auto* aSliderAction = new QWidgetAction(this);
    aSliderAction->setDefaultWidget(aSliderWidget);
    aSettingsMenu->addAction(aSliderAction);

    connect(m_fuzzySlider, &QSlider::valueChanged, this, [this](int val) {
        m_fuzzyThreshold = static_cast<double>(val);
        m_fuzzyValueLabel->setText(QString::number(val) + "%");
        if (m_save.isLoaded()) applyPartyFilter();
    });
}

void MainWindow::setupStatusBar(){
    m_statusLabel = new QLabel("Ready");
    statusBar()->addWidget(m_statusLabel);

    m_warningLabel = new QLabel("\u26A0");
    QFont wf = m_warningLabel->font();
    wf.setPointSize(16);
    wf.setBold(true);
    m_warningLabel->setFont(wf);
    m_warningLabel->setForegroundRole(QPalette::BrightText);
    m_warningLabel->setToolTip("This folder may not be a valid RPG Maker game distribution");
    m_warningLabel->setCursor(Qt::ArrowCursor);
    m_warningLabel->hide();
    statusBar()->addPermanentWidget(m_warningLabel);
}

// --- Drag and Drop events ---

void MainWindow::dragEnterEvent(QDragEnterEvent* event){
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event){
    const auto aUrls = event->mimeData()->urls();
    if (aUrls.isEmpty()) return;
    handleDrop(aUrls.first().toLocalFile());
}

// --- Locale ---

void MainWindow::onLocaleChanged(int iIndex){
    if (iIndex < 0) return;
    QString locale = m_localeCombo->itemData(iIndex).toString();
    LOG_INFO("Locale changed to: {}", locale.toStdString());
    m_save.setActiveLocale(locale);
}

void MainWindow::updateLocaleCombo(){
    m_localeCombo->blockSignals(true);
    m_localeCombo->clear();

    QStringList locales = m_save.availableLocales();
    int iSelectIdx = 0;
    for (int i = 0; i < locales.size(); ++i) {
        QString display = locales[i];
        if (display == "native") display = "Native";
        m_localeCombo->addItem(display, locales[i]);
        if (locales[i] == m_save.activeLocale()) iSelectIdx = i;
    }
    m_localeCombo->setCurrentIndex(iSelectIdx);
    m_localeCombo->blockSignals(false);
}

// --- About ---

void MainWindow::onAbout(){
    QDialog dlg(this);
    dlg.setWindowTitle("About RPGSaveEditor");

    auto* aLayout = new QVBoxLayout(&dlg);

    auto* aLabel = new QLabel(
        "<p>RPGSaveEditor v1.0</p>"
        "<p>A cross-platform RPG Maker MV save file editor.</p>"
        "<p>Drag &amp; drop a game root folder or .rpgsave file.<br>"
        "Saves are written to a new slot to prevent overwriting.</p>"
        "<hr>"
        "<p>Author: <a href=\"https://github.com/PetricaT\">PetricaT</a></p>");
    aLabel->setTextFormat(Qt::RichText);
    aLabel->setOpenExternalLinks(true);
    aLabel->setWordWrap(true);
    aLayout->addWidget(aLabel);

    auto* aButtonBox = new QDialogButtonBox(QDialogButtonBox::Ok);
    aLayout->addWidget(aButtonBox);
    QObject::connect(aButtonBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);

    dlg.setMinimumWidth(400);
    dlg.exec();
}

// --- Helpers ---

int MainWindow::askUserForSaveSlot(const QStringList& saves){
    QStringList items;
    for (const auto& aS : saves) {
        items << QFileInfo(aS).fileName();
    }
    bool bOk = false;
    QString item = QInputDialog::getItem(this, "Select Save Slot",
        "Multiple save files found. Which one to load?",
        items, 0, false, &bOk);
    if (!bOk || item.isEmpty()) return -1;
    return saves.indexOf(QDir(m_saveDir).absoluteFilePath(item));
}

void MainWindow::setTitle(){
    if (m_bDirty) {
        setWindowTitle("RPGSaveEditor (*)");
    } else {
        setWindowTitle("RPGSaveEditor");
    }
}

void MainWindow::setStatus(const QString& msg){
    LOG_INFO("Status: {}", msg.toStdString());
    m_statusLabel->setText(msg);
}

// --- Close ---

bool MainWindow::confirmDiscard(){
    if (!m_bDirty) return true;

    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle("Unsaved Changes");
    box.setText("There are unsaved changes.");
    box.setInformativeText("Do you want to save before closing?");
    box.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    box.setDefaultButton(QMessageBox::Save);
    box.exec();

    auto aBtn = box.standardButton(box.clickedButton());
    if (aBtn == QMessageBox::Save) {
        onSave();
        return !m_bDirty;
    }
    return aBtn == QMessageBox::Discard;
}

void MainWindow::onClose(){
    if (!m_save.isLoaded()) return;
    if (!confirmDiscard()) return;

    m_save.reset();
    m_bDirty = false;
    m_saveDir.clear();
    m_gameRoot.clear();
    m_warningLabel->hide();
    m_stacked->setCurrentWidget(m_welcomeWidget);
    setTitle();
    setStatus("Ready");
}

void MainWindow::closeEvent(QCloseEvent* event){
    if (!m_save.isLoaded() || confirmDiscard()) {
        event->accept();
    } else {
        event->ignore();
    }
}
