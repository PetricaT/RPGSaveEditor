#include "mainwindow.h"
#include "logger.h"

#include <rapidfuzz/fuzz.hpp>

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QSplitter>
#include <QVBoxLayout>

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
        if (column <= 1 || column == 8) return;
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

// --- Filter ---

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
