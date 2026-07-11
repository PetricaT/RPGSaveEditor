#pragma once

#include <QAbstractItemModel>
#include <QModelIndex>
#include <QVariant>
#include <nlohmann/json.hpp>

using json = nlohmann::ordered_json;

class JsonModel : public QAbstractItemModel {
    Q_OBJECT
public:
    explicit JsonModel(QObject* parent = nullptr);
    ~JsonModel() override;

    void setJson(const json& root);
    void clear();

    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;
    QModelIndex parent(const QModelIndex& index) const override;
    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    json getJson() const;

private:
    struct JsonNode;
    JsonNode* nodeFromIndex(const QModelIndex& index) const;

    JsonNode* m_rootNode = nullptr;
};
