#include "mainwindow.h"
#include "logger.h"

#include <rapidfuzz/fuzz.hpp>

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QSplitter>
#include <QVBoxLayout>

void MainWindow::setupTabs(){
    // --- Variables Tab ---
    auto* aVarTab = new QWidget;
    auto* aVarLayout = new QVBoxLayout(aVarTab);

    m_varFilter = new QLineEdit;
    m_varFilter->setPlaceholderText("Filter variables...");
    m_varFilter->setClearButtonEnabled(true);
    aVarLayout->addWidget(m_varFilter);

    m_varTable = new QTableWidget;
    m_varTable->setColumnCount(3);
    m_varTable->setHorizontalHeaderLabels({"ID", "Name", "Value"});
    m_varTable->horizontalHeader()->setStretchLastSection(true);
    m_varTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_varTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_varTable->setSelectionBehavior(QTableWidget::SelectRows);
    m_varTable->setAlternatingRowColors(true);
    m_varTable->verticalHeader()->setVisible(false);
    aVarLayout->addWidget(m_varTable);

    connect(m_varFilter, &QLineEdit::textChanged, this, [this](const QString& text) {
        for (int i = 0; i < m_varTable->rowCount(); ++i) {
            bool bMatch = text.isEmpty();
            if (!bMatch) {
                for (int iC = 0; iC < m_varTable->columnCount(); ++iC) {
                    auto* aItem = m_varTable->item(i, iC);
                    if (aItem && aItem->text().contains(text, Qt::CaseInsensitive)) {
                        bMatch = true;
                        break;
                    }
                }
            }
            m_varTable->setRowHidden(i, !bMatch);
        }
    });

    connect(m_varTable, &QTableWidget::cellChanged, this, [this](int iRow, int iColumn) {
        if (iColumn != 2) return;
        auto* aItem = m_varTable->item(iRow, iColumn);
        if (!aItem) return;
        int iId = m_varTable->item(iRow, 0)->text().toInt();
        QString name = m_varTable->item(iRow, 1) ? m_varTable->item(iRow, 1)->text() : QString();
        QString valStr = aItem->text();

        json oldVal = m_save.getVariable(iId);
        QString oldStr;
        if (oldVal.is_null()) oldStr = "null";
        else if (oldVal.is_boolean()) oldStr = oldVal.get<bool>() ? "true" : "false";
        else if (oldVal.is_number_integer()) oldStr = QString::number(oldVal.get<int64_t>());
        else if (oldVal.is_number_float()) oldStr = QString::number(oldVal.get<double>(), 'g', 6);
        else if (oldVal.is_string()) oldStr = QString::fromStdString(oldVal.get<std::string>());
        else oldStr = "...";

        LOG_INFO("Editing variable with name: \"{}\", value: \"{}\", new value: \"{}\"", name.toStdString(), oldStr.toStdString(), valStr.toStdString());
        bool bOk = false;
        int64_t iIntVal = valStr.toLongLong(&bOk);
        if (bOk) { m_save.setVariable(iId, iIntVal); return; }
        double dDblVal = valStr.toDouble(&bOk);
        if (bOk) { m_save.setVariable(iId, dDblVal); return; }
        if (valStr.toLower() == "true") { m_save.setVariable(iId, true); return; }
        if (valStr.toLower() == "false") { m_save.setVariable(iId, false); return; }
        if (valStr.toLower() == "null") { m_save.setVariable(iId, nullptr); return; }
        m_save.setVariable(iId, valStr.toStdString());
    });

    m_tabs->addTab(aVarTab, "Variables");

    // --- Switches Tab ---
    auto* aSwTab = new QWidget;
    auto* aSwLayout = new QVBoxLayout(aSwTab);

    m_swFilter = new QLineEdit;
    m_swFilter->setPlaceholderText("Filter switches...");
    m_swFilter->setClearButtonEnabled(true);
    aSwLayout->addWidget(m_swFilter);

    m_swTable = new QTableWidget;
    m_swTable->setColumnCount(3);
    m_swTable->setHorizontalHeaderLabels({"ID", "Name", "Enabled"});
    m_swTable->horizontalHeader()->setStretchLastSection(true);
    m_swTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_swTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_swTable->setSelectionBehavior(QTableWidget::SelectRows);
    m_swTable->setAlternatingRowColors(true);
    m_swTable->verticalHeader()->setVisible(false);
    aSwLayout->addWidget(m_swTable);

    connect(m_swFilter, &QLineEdit::textChanged, this, [this](const QString& text) {
        for (int i = 0; i < m_swTable->rowCount(); ++i) {
            bool bMatch = text.isEmpty();
            if (!bMatch) {
                for (int iC = 0; iC < m_swTable->columnCount(); ++iC) {
                    auto* aItem = m_swTable->item(i, iC);
                    if (aItem && aItem->text().contains(text, Qt::CaseInsensitive)) {
                        bMatch = true;
                        break;
                    }
                }
            }
            m_swTable->setRowHidden(i, !bMatch);
        }
    });

    connect(m_swTable, &QTableWidget::cellChanged, this, [this](int iRow, int iColumn) {
        if (iColumn != 2) return;
        auto* aItem = m_swTable->item(iRow, iColumn);
        if (!aItem) return;
        int iId = m_swTable->item(iRow, 0)->text().toInt();
        QString name = m_swTable->item(iRow, 1) ? m_swTable->item(iRow, 1)->text() : QString();
        bool bNewVal = aItem->text().toLower() == "true" || aItem->text() == "1";
        bool bOldVal = m_save.getSwitch(iId);
        LOG_INFO("Editing switch with name: \"{}\", value: \"{}\", new value: \"{}\"", name.toStdString(), bOldVal ? "true" : "false", bNewVal ? "true" : "false");
        m_save.setSwitch(iId, bNewVal);
    });

    m_tabs->addTab(aSwTab, "Switches");

    // --- Party Tab ---
    auto* aPartyTab = new QWidget;
    auto* aPartyLayout = new QVBoxLayout(aPartyTab);

    auto* aGoldLayout = new QHBoxLayout;
    aGoldLayout->addWidget(new QLabel("Gold:"));
    auto* aGoldEdit = new QLineEdit;
    aGoldEdit->setObjectName("goldEdit");
    aGoldLayout->addWidget(aGoldEdit);
    aGoldLayout->addStretch();
    aGoldLayout->addWidget(new QLabel("Steps:"));
    auto* aStepsEdit = new QLineEdit;
    aStepsEdit->setObjectName("stepsEdit");
    aGoldLayout->addWidget(aStepsEdit);
    aGoldLayout->addStretch();
    aPartyLayout->addLayout(aGoldLayout);

    connect(aGoldEdit, &QLineEdit::editingFinished, this, [this, aGoldEdit]() {
        m_save.setGold(aGoldEdit->text().toInt());
    });
    connect(aStepsEdit, &QLineEdit::editingFinished, this, [this, aStepsEdit]() {
        m_save.setSteps(aStepsEdit->text().toInt());
    });

    auto* aPartySplitter = new QSplitter(Qt::Vertical);

    m_itemTable = new QTableWidget;
    m_itemTable->setColumnCount(3);
    m_itemTable->setHorizontalHeaderLabels({"ID", "Name", "Quantity"});
    m_itemTable->horizontalHeader()->setStretchLastSection(true);
    m_itemTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_itemTable->setAlternatingRowColors(true);
    m_itemTable->verticalHeader()->setVisible(false);
    auto* aIw = new QWidget;
    auto* aIl = new QVBoxLayout(aIw);
    aIl->setContentsMargins(0, 0, 0, 0);
    aIl->addWidget(new QLabel("Items"));
    aIl->addWidget(m_itemTable);
    aPartySplitter->addWidget(aIw);

    m_weaponTable = new QTableWidget;
    m_weaponTable->setColumnCount(3);
    m_weaponTable->setHorizontalHeaderLabels({"ID", "Name", "Quantity"});
    m_weaponTable->horizontalHeader()->setStretchLastSection(true);
    m_weaponTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_weaponTable->setAlternatingRowColors(true);
    m_weaponTable->verticalHeader()->setVisible(false);
    auto* aWw = new QWidget;
    auto* aWl = new QVBoxLayout(aWw);
    aWl->setContentsMargins(0, 0, 0, 0);
    aWl->addWidget(new QLabel("Weapons"));
    aWl->addWidget(m_weaponTable);
    aPartySplitter->addWidget(aWw);

    m_armorTable = new QTableWidget;
    m_armorTable->setColumnCount(3);
    m_armorTable->setHorizontalHeaderLabels({"ID", "Name", "Quantity"});
    m_armorTable->horizontalHeader()->setStretchLastSection(true);
    m_armorTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_armorTable->setAlternatingRowColors(true);
    m_armorTable->verticalHeader()->setVisible(false);
    auto* aAw = new QWidget;
    auto* aAl = new QVBoxLayout(aAw);
    aAl->setContentsMargins(0, 0, 0, 0);
    aAl->addWidget(new QLabel("Armors"));
    aAl->addWidget(m_armorTable);
    aPartySplitter->addWidget(aAw);

    auto* aPartyFilterLayout = new QHBoxLayout;
    aPartyFilterLayout->addWidget(new QLabel("Search:"));
    m_partyFilter = new QLineEdit;
    m_partyFilter->setPlaceholderText("Fuzzy search items, weapons, armors...");
    aPartyFilterLayout->addWidget(m_partyFilter);
    aPartyLayout->addLayout(aPartyFilterLayout);

    connect(m_partyFilter, &QLineEdit::textChanged, this, &MainWindow::applyPartyFilter);

    aPartyLayout->addWidget(aPartySplitter);

    auto aApplyMultiQuantity = [](QTableWidget* table, int iTriggeredRow, int iColumn, int iVal) {
        QSet<int> applied;
        applied.insert(iTriggeredRow);
        auto aSelItems = table->selectedItems();
        if (aSelItems.size() <= 1) return;
        table->blockSignals(true);
        for (auto* aSelItem : aSelItems) {
            if (aSelItem->column() == iColumn && !applied.contains(aSelItem->row())) {
                applied.insert(aSelItem->row());
                aSelItem->setText(QString::number(iVal));
            }
        }
        table->blockSignals(false);
    };

    auto aSaveSelItems = [this](QTableWidget* table, const std::function<void(int,int)>& saveFn) {
        for (auto* aSelItem : table->selectedItems()) {
            if (aSelItem->column() == 2) {
                int iId = table->item(aSelItem->row(), 0)->text().toInt();
                saveFn(iId, aSelItem->text().toInt());
            }
        }
    };

    connect(m_itemTable, &QTableWidget::cellChanged, this, [this, aApplyMultiQuantity, aSaveSelItems](int iRow, int iColumn) {
        if (iColumn != 2) return;
        auto* aItem = m_itemTable->item(iRow, iColumn);
        if (!aItem) return;
        int iVal = aItem->text().toInt();
        aApplyMultiQuantity(m_itemTable, iRow, iColumn, iVal);
        aSaveSelItems(m_itemTable, [this](int iId, int iQty) { m_save.setItem(iId, iQty); });
    });
    connect(m_weaponTable, &QTableWidget::cellChanged, this, [this, aApplyMultiQuantity, aSaveSelItems](int iRow, int iColumn) {
        if (iColumn != 2) return;
        auto* aItem = m_weaponTable->item(iRow, iColumn);
        if (!aItem) return;
        int iVal = aItem->text().toInt();
        aApplyMultiQuantity(m_weaponTable, iRow, iColumn, iVal);
        aSaveSelItems(m_weaponTable, [this](int iId, int iQty) { m_save.setWeapon(iId, iQty); });
    });
    connect(m_armorTable, &QTableWidget::cellChanged, this, [this, aApplyMultiQuantity, aSaveSelItems](int iRow, int iColumn) {
        if (iColumn != 2) return;
        auto* aItem = m_armorTable->item(iRow, iColumn);
        if (!aItem) return;
        int iVal = aItem->text().toInt();
        aApplyMultiQuantity(m_armorTable, iRow, iColumn, iVal);
        aSaveSelItems(m_armorTable, [this](int iId, int iQty) { m_save.setArmor(iId, iQty); });
    });

    m_tabs->addTab(aPartyTab, "Party");

    // --- Actors Tab ---
    m_actorTable = new QTableWidget;
    m_actorTable->setColumnCount(9);
    m_actorTable->setHorizontalHeaderLabels({"ID", "Name", "HP", "Max HP", "MP", "Max MP", "TP", "Level", "EXP"});
    m_actorTable->horizontalHeader()->setStretchLastSection(true);
    m_actorTable->setAlternatingRowColors(true);
    m_actorTable->verticalHeader()->setVisible(false);
    m_tabs->addTab(m_actorTable, "Actors");

    connect(m_actorTable, &QTableWidget::cellChanged, this, [this](int iRow, int iColumn) {
        if (iColumn <= 1 || iColumn == 8) return;
        int iId = m_actorTable->item(iRow, 0)->text().toInt();
        json actor = m_save.getActor(iId);
        if (actor.is_null()) return;
        auto* aItem = m_actorTable->item(iRow, iColumn);
        if (!aItem) return;
        int iVal = aItem->text().toInt();
        switch (iColumn) {
            case 2: actor["_hp"] = iVal; break;
            case 3: actor["_maxHp"] = iVal; break;
            case 4: actor["_mp"] = iVal; break;
            case 5: actor["_maxMp"] = iVal; break;
            case 6: actor["_tp"] = iVal; break;
            case 7: actor["_level"] = iVal; break;
        }
        m_save.setActor(iId, actor);
    });

    // --- JSON Tree Tab ---
    m_jsonTree = new QTreeWidget;
    m_jsonTree->setHeaderLabels({"Key", "Value"});
    m_jsonTree->setAlternatingRowColors(true);
    m_tabs->addTab(m_jsonTree, "JSON Tree");
}

// --- Tab population ---

void MainWindow::populateAllTabs(){
    LOG_DEBUG("populateAllTabs");
    populateVariablesTab();
    populateSwitchesTab();
    populatePartyTab();
    populateActorsTab();
    refreshJsonTree();
    m_stacked->setCurrentWidget(m_tabs);
}

void MainWindow::populateVariablesTab(){
    LOG_DEBUG("populateVariablesTab");
    m_varTable->blockSignals(true);
    m_varTable->setRowCount(0);

    int iCount = m_save.variableCount();
    for (int i = 0; i < iCount; ++i) {
        json val = m_save.getVariable(i);
        QString name = m_save.variableName(i);
        bool bHasName = !name.startsWith("Var ");
        if (!bHasName && (val.is_null() || (val.is_number() && val.get<double>() == 0)))
            continue;

        int iRow = m_varTable->rowCount();
        m_varTable->insertRow(iRow);

        auto* aIdItem = new QTableWidgetItem(QString::number(i));
        aIdItem->setFlags(aIdItem->flags() & ~Qt::ItemIsEditable);
        m_varTable->setItem(iRow, 0, aIdItem);

        auto* aNameItem = new QTableWidgetItem(name);
        aNameItem->setFlags(aNameItem->flags() & ~Qt::ItemIsEditable);
        if (bHasName) aNameItem->setForeground(m_varTable->palette().color(QPalette::Link));
        m_varTable->setItem(iRow, 1, aNameItem);

        QString valStr;
        if (val.is_null()) valStr = "null";
        else if (val.is_boolean()) valStr = val.get<bool>() ? "true" : "false";
        else if (val.is_number_integer()) valStr = QString::number(val.get<int64_t>());
        else if (val.is_number_float()) valStr = QString::number(val.get<double>(), 'g', 6);
        else if (val.is_string()) valStr = QString::fromStdString(val.get<std::string>());
        else valStr = "...";

        m_varTable->setItem(iRow, 2, new QTableWidgetItem(valStr));
    }
    m_varTable->resizeColumnsToContents();
    m_varTable->blockSignals(false);
}

void MainWindow::populateSwitchesTab(){
    LOG_DEBUG("populateSwitchesTab");
    m_swTable->blockSignals(true);
    m_swTable->setRowCount(0);

    int iCount = m_save.switchCount();
    for (int i = 0; i < iCount; ++i) {
        bool bVal = m_save.getSwitch(i);
        QString name = m_save.switchName(i);
        bool bHasName = !name.startsWith("Switch ");
        if (!bHasName && !bVal) continue;

        int iRow = m_swTable->rowCount();
        m_swTable->insertRow(iRow);

        auto* aIdItem = new QTableWidgetItem(QString::number(i));
        aIdItem->setFlags(aIdItem->flags() & ~Qt::ItemIsEditable);
        m_swTable->setItem(iRow, 0, aIdItem);

        auto* aNameItem = new QTableWidgetItem(name);
        aNameItem->setFlags(aNameItem->flags() & ~Qt::ItemIsEditable);
        if (bHasName) aNameItem->setForeground(m_swTable->palette().color(QPalette::Link));
        m_swTable->setItem(iRow, 1, aNameItem);

        m_swTable->setItem(iRow, 2, new QTableWidgetItem(bVal ? "true" : "false"));
    }
    m_swTable->resizeColumnsToContents();
    m_swTable->blockSignals(false);
}

void MainWindow::populatePartyTab(){
    LOG_DEBUG("populatePartyTab");
    auto* aGoldEdit = findChild<QLineEdit*>("goldEdit");
    auto* aStepsEdit = findChild<QLineEdit*>("stepsEdit");
    if (aGoldEdit) aGoldEdit->setText(QString::number(m_save.gold()));
    if (aStepsEdit) aStepsEdit->setText(QString::number(m_save.steps()));

    m_itemTable->blockSignals(true);
    m_itemTable->setRowCount(0);
    auto aItems = m_save.items();
    for (auto aIt = aItems.constBegin(); aIt != aItems.constEnd(); ++aIt) {
        int iRow = m_itemTable->rowCount();
        m_itemTable->insertRow(iRow);
        auto* aIdItem = new QTableWidgetItem(QString::number(aIt.key()));
        aIdItem->setFlags(aIdItem->flags() & ~Qt::ItemIsEditable);
        m_itemTable->setItem(iRow, 0, aIdItem);
        auto* aNameItem = new QTableWidgetItem(m_save.itemName(aIt.key()));
        aNameItem->setFlags(aNameItem->flags() & ~Qt::ItemIsEditable);
        m_itemTable->setItem(iRow, 1, aNameItem);
        m_itemTable->setItem(iRow, 2, new QTableWidgetItem(QString::number(aIt.value())));
    }
    m_itemTable->resizeColumnsToContents();
    m_itemTable->blockSignals(false);

    m_weaponTable->blockSignals(true);
    m_weaponTable->setRowCount(0);
    auto aWeapons = m_save.weapons();
    for (auto aIt = aWeapons.constBegin(); aIt != aWeapons.constEnd(); ++aIt) {
        int iRow = m_weaponTable->rowCount();
        m_weaponTable->insertRow(iRow);
        auto* aIdItem = new QTableWidgetItem(QString::number(aIt.key()));
        aIdItem->setFlags(aIdItem->flags() & ~Qt::ItemIsEditable);
        m_weaponTable->setItem(iRow, 0, aIdItem);
        auto* aNameItem = new QTableWidgetItem(m_save.weaponName(aIt.key()));
        aNameItem->setFlags(aNameItem->flags() & ~Qt::ItemIsEditable);
        m_weaponTable->setItem(iRow, 1, aNameItem);
        m_weaponTable->setItem(iRow, 2, new QTableWidgetItem(QString::number(aIt.value())));
    }
    m_weaponTable->resizeColumnsToContents();
    m_weaponTable->blockSignals(false);

    m_armorTable->blockSignals(true);
    m_armorTable->setRowCount(0);
    auto aArmors = m_save.armors();
    for (auto aIt = aArmors.constBegin(); aIt != aArmors.constEnd(); ++aIt) {
        int iRow = m_armorTable->rowCount();
        m_armorTable->insertRow(iRow);
        auto* aIdItem = new QTableWidgetItem(QString::number(aIt.key()));
        aIdItem->setFlags(aIdItem->flags() & ~Qt::ItemIsEditable);
        m_armorTable->setItem(iRow, 0, aIdItem);
        auto* aNameItem = new QTableWidgetItem(m_save.armorName(aIt.key()));
        aNameItem->setFlags(aNameItem->flags() & ~Qt::ItemIsEditable);
        m_armorTable->setItem(iRow, 1, aNameItem);
        m_armorTable->setItem(iRow, 2, new QTableWidgetItem(QString::number(aIt.value())));
    }
    m_armorTable->resizeColumnsToContents();
    m_armorTable->blockSignals(false);
}

void MainWindow::populateActorsTab(){
    LOG_DEBUG("populateActorsTab");
    m_actorTable->blockSignals(true);
    m_actorTable->setRowCount(0);

    auto aSafeNum = [](const json& obj, const char* key, int iFallback) -> int {
        try {
            if (!obj.contains(key)) return iFallback;
            const auto& aVal = obj[key];
            if (aVal.is_number_integer()) return aVal.get<int>();
            if (aVal.is_number_unsigned()) return static_cast<int>(aVal.get<uint64_t>());
            if (aVal.is_number_float()) return static_cast<int>(aVal.get<double>());
            return iFallback;
        } catch (...) { return iFallback; }
    };

    auto aSafeExp = [](const json& obj, int iFallback) -> int {
        try {
            if (!obj.contains("_exp")) return iFallback;
            const auto& aExp = obj["_exp"];
            if (aExp.is_number()) return aExp.get<int>();
            if (aExp.is_object() && aExp.contains("1"))
                return aExp["1"].is_number() ? aExp["1"].get<int>() : iFallback;
            return iFallback;
        } catch (...) { return iFallback; }
    };

    int iMaxId = m_save.maxActorId();
    if (iMaxId <= 0) {
        for (int iId = 1; iId < 100; ++iId) {
            json actor = m_save.getActor(iId);
            if (actor.is_null() || !actor.is_object()) continue;
            int iRow = m_actorTable->rowCount();
            m_actorTable->insertRow(iRow);
            auto aAddCell = [&](int iCol, const QVariant& val, bool bEditable = false) {
                auto* aItem = new QTableWidgetItem(val.toString());
                if (!bEditable) aItem->setFlags(aItem->flags() & ~Qt::ItemIsEditable);
                m_actorTable->setItem(iRow, iCol, aItem);
            };
            aAddCell(0, iId);
            aAddCell(1, m_save.actorName(iId));
            aAddCell(2, aSafeNum(actor, "_hp", 0), true);
            aAddCell(3, aSafeNum(actor, "_maxHp", 0), true);
            aAddCell(4, aSafeNum(actor, "_mp", 0), true);
            aAddCell(5, aSafeNum(actor, "_maxMp", 0), true);
            aAddCell(6, aSafeNum(actor, "_tp", 0), true);
            aAddCell(7, aSafeNum(actor, "_level", 0), true);
            aAddCell(8, aSafeExp(actor, 0));
        }
    } else {
        for (int iId = 1; iId <= iMaxId; ++iId) {
            if (!m_save.actorName(iId).startsWith("Actor ")) {
                json actor = m_save.getActor(iId);
                int iRow = m_actorTable->rowCount();
                m_actorTable->insertRow(iRow);
                auto aAddCell = [&](int iCol, const QVariant& val, bool bEditable = false) {
                    auto* aItem = new QTableWidgetItem(val.toString());
                    if (!bEditable) aItem->setFlags(aItem->flags() & ~Qt::ItemIsEditable);
                    m_actorTable->setItem(iRow, iCol, aItem);
                };
                bool bHasSaveData = actor.is_object() && !actor.is_null();
                aAddCell(0, iId);
                aAddCell(1, m_save.actorName(iId));
                aAddCell(2, bHasSaveData ? aSafeNum(actor, "_hp", 0) : 0, true);
                aAddCell(3, bHasSaveData ? aSafeNum(actor, "_maxHp", 0) : 0, true);
                aAddCell(4, bHasSaveData ? aSafeNum(actor, "_mp", 0) : 0, true);
                aAddCell(5, bHasSaveData ? aSafeNum(actor, "_maxMp", 0) : 0, true);
                aAddCell(6, bHasSaveData ? aSafeNum(actor, "_tp", 0) : 0, true);
                aAddCell(7, bHasSaveData ? aSafeNum(actor, "_level", 0) : 0, true);
                aAddCell(8, bHasSaveData ? aSafeExp(actor, 0) : 0);
            }
        }
    }
    m_actorTable->resizeColumnsToContents();
    m_actorTable->blockSignals(false);
}

void MainWindow::refreshJsonTree(){
    LOG_DEBUG("refreshJsonTree");
    m_jsonTree->clear();
    if (!m_save.isLoaded()) return;

    const json& root = m_save.root();
    std::function<void(QTreeWidgetItem*, const QString&, const json&)> addJsonNode;
    addJsonNode = [&](QTreeWidgetItem* parent, const QString& key, const json& val) {
        auto* aItem = new QTreeWidgetItem(parent);
        aItem->setText(0, key);

        if (val.is_object()) {
            aItem->setText(1, QString("{ %1 items }").arg(val.size()));
            for (auto aIt = val.begin(); aIt != val.end(); ++aIt)
                addJsonNode(aItem, QString::fromStdString(aIt.key()), *aIt);
        } else if (val.is_array()) {
            aItem->setText(1, QString("[ %1 items ]").arg(val.size()));
            for (size_t i = 0; i < val.size(); ++i)
                addJsonNode(aItem, QString("[%1]").arg(i), val[i]);
        } else if (val.is_string()) {
            aItem->setText(1, QString::fromStdString(val.get<std::string>()));
        } else if (val.is_number_integer()) {
            aItem->setText(1, QString::number(val.get<int64_t>()));
        } else if (val.is_number_float()) {
            aItem->setText(1, QString::number(val.get<double>(), 'g', 6));
        } else if (val.is_boolean()) {
            aItem->setText(1, val.get<bool>() ? "true" : "false");
        } else if (val.is_null()) {
            aItem->setText(1, "null");
        }
    };

    for (auto aIt = root.begin(); aIt != root.end(); ++aIt)
        addJsonNode(m_jsonTree->invisibleRootItem(), QString::fromStdString(aIt.key()), *aIt);
}

// --- Filter ---

void MainWindow::applyPartyFilter(){
    QString query = m_partyFilter->text().trimmed();
    if (query.isEmpty()) {
        for (auto* aT : {m_itemTable, m_weaponTable, m_armorTable}) {
            for (int iR = 0; iR < aT->rowCount(); ++iR)
                aT->setRowHidden(iR, false);
        }
        return;
    }

    std::string qStd = query.toLower().toStdString();
    for (auto* aT : {m_itemTable, m_weaponTable, m_armorTable}) {
        for (int iR = 0; iR < aT->rowCount(); ++iR) {
            auto* aNameItem = aT->item(iR, 1);
            if (!aNameItem) { aT->setRowHidden(iR, true); continue; }
            std::string name = aNameItem->text().toLower().toStdString();
            double dScore = rapidfuzz::fuzz::partial_ratio(qStd, name);
            aT->setRowHidden(iR, dScore < m_fuzzyThreshold);
        }
    }
}
