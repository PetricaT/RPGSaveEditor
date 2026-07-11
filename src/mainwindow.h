#pragma once

#include "rpgsave.h"
#include "json_model.h"

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QTreeWidget>

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override = default;

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onSave();
    void onSaveAs();
    void onClose();
    void onLoadGameData();
    void onLoadTranslations();
    void onExportTranslationTemplate();
    void onLocaleChanged(int index);
    void onAbout();

private:
    void setupUI();
    void setupMenuBar();
    void setupTabs();
    void setupStatusBar();

    void handleDrop(const QString& path);
    void loadFromGameRoot(const QString& gameRoot);
    void loadFromSaveFile(const QString& saveFilePath);
    void loadGameDataFromRoot(const QString& gameRoot);

    void populateAllTabs();
    void populateVariablesTab();
    void populateSwitchesTab();
    void populatePartyTab();
    void populateActorsTab();
    void refreshJsonTree();
    void applyPartyFilter();

    void setTitle();
    void setStatus(const QString& msg);
    bool confirmDiscard();

    int askUserForSaveSlot(const QStringList& saveFiles);
    void updateLocaleCombo();

    RPGSave m_save;
    QString m_saveDir;
    QString m_gameRoot;
    bool m_bDirty = false;

    // UI elements
    QTabWidget* m_tabs = nullptr;
    QTableWidget* m_varTable = nullptr;
    QLineEdit* m_varFilter = nullptr;
    QTableWidget* m_swTable = nullptr;
    QLineEdit* m_swFilter = nullptr;
    QTableWidget* m_itemTable = nullptr;
    QTableWidget* m_weaponTable = nullptr;
    QTableWidget* m_armorTable = nullptr;
    QLineEdit* m_partyFilter = nullptr;
    QTableWidget* m_actorTable = nullptr;
    QTreeWidget* m_jsonTree = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_warningLabel = nullptr;
    QStackedWidget* m_stacked = nullptr;
    QWidget* m_welcomeWidget = nullptr;
    QComboBox* m_localeCombo = nullptr;
};
