#include "json_model.h"

#include <stdexcept>

struct JsonModel::JsonNode {
    json value;
    std::string key;
    JsonNode* parent = nullptr;
    std::vector<JsonNode*> children;

    JsonNode(const json& val = nullptr, const std::string& k = "", JsonNode* p = nullptr)
        : value(val), key(k), parent(p)
    {
    }

    ~JsonNode()
    {
        for (auto* child : children) {
            delete child;
        }
    }

    void buildChildren()
    {
        for (auto* c : children) delete c;
        children.clear();

        if (value.is_object()) {
            for (auto it = value.begin(); it != value.end(); ++it) {
                auto* child = new JsonNode(*it, it.key(), this);
                child->buildChildren();
                children.push_back(child);
            }
        } else if (value.is_array()) {
            for (size_t i = 0; i < value.size(); ++i) {
                auto* child = new JsonNode(value[i], std::to_string(i), this);
                child->buildChildren();
                children.push_back(child);
            }
        }
    }

    int childIndex() const
    {
        if (!parent) return 0;
        for (int i = 0; i < static_cast<int>(parent->children.size()); ++i) {
            if (parent->children[i] == this) return i;
        }
        return 0;
    }

    void updateParentValue()
    {
        if (!parent) return;

        if (parent->value.is_object()) {
            parent->value[key] = value;
        } else if (parent->value.is_array()) {
            size_t idx = static_cast<size_t>(std::stoi(key));
            if (idx < parent->value.size()) {
                parent->value[idx] = value;
            }
        }

        parent->updateParentValue();
    }
};

JsonModel::JsonModel(QObject* parent)
    : QAbstractItemModel(parent)
{
}

JsonModel::~JsonModel()
{
    delete m_rootNode;
}

void JsonModel::setJson(const json& root)
{
    beginResetModel();
    delete m_rootNode;
    m_rootNode = new JsonNode(root, "root", nullptr);
    m_rootNode->buildChildren();
    endResetModel();
}

void JsonModel::clear()
{
    beginResetModel();
    delete m_rootNode;
    m_rootNode = nullptr;
    endResetModel();
}

JsonModel::JsonNode* JsonModel::nodeFromIndex(const QModelIndex& index) const
{
    if (!index.isValid()) return m_rootNode;
    return static_cast<JsonNode*>(index.internalPointer());
}

QModelIndex JsonModel::index(int row, int column, const QModelIndex& parent) const
{
    if (!m_rootNode) return {};
    if (column != 0) return {};

    JsonNode* parentNode = nodeFromIndex(parent);
    if (!parentNode) parentNode = m_rootNode;

    if (row < 0 || row >= static_cast<int>(parentNode->children.size())) return {};

    return createIndex(row, 0, parentNode->children[row]);
}

QModelIndex JsonModel::parent(const QModelIndex& index) const
{
    if (!index.isValid()) return {};

    JsonNode* node = nodeFromIndex(index);
    if (!node || !node->parent || node->parent == m_rootNode) return {};

    return createIndex(node->parent->childIndex(), 0, node->parent);
}

int JsonModel::rowCount(const QModelIndex& parent) const
{
    if (!m_rootNode) return 0;
    JsonNode* node = nodeFromIndex(parent);
    if (!node) node = m_rootNode;
    return static_cast<int>(node->children.size());
}

int JsonModel::columnCount(const QModelIndex& /*parent*/) const
{
    return 2; // key + value
}

QVariant JsonModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || !m_rootNode) return {};

    JsonNode* node = nodeFromIndex(index);
    if (!node) return {};

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        if (index.column() == 0) {
            // Key column
            if (node->parent == m_rootNode) {
                return QString::fromStdString(node->key);
            }
            // For array children, show index
            if (node->parent && node->parent->value.is_array()) {
                return QString("[%1]").arg(QString::fromStdString(node->key));
            }
            return QString::fromStdString(node->key);
        } else {
            // Value column
            if (node->value.is_string()) {
                return QString::fromStdString(node->value.get<std::string>());
            } else if (node->value.is_number_integer()) {
                return qlonglong(node->value.get<int64_t>());
            } else if (node->value.is_number_float()) {
                return node->value.get<double>();
            } else if (node->value.is_boolean()) {
                return node->value.get<bool>() ? "true" : "false";
            } else if (node->value.is_null()) {
                return "null";
            } else if (node->value.is_object()) {
                return QString("{ %1 items }").arg(node->value.size());
            } else if (node->value.is_array()) {
                return QString("[ %1 items ]").arg(node->value.size());
            }
        }
    }

    if (role == Qt::CheckStateRole && index.column() == 1) {
        if (node->value.is_boolean()) {
            return node->value.get<bool>() ? Qt::Checked : Qt::Unchecked;
        }
    }

    return {};
}

QVariant JsonModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        return section == 0 ? "Key" : "Value";
    }
    return {};
}

bool JsonModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (!index.isValid() || !m_rootNode) return false;

    JsonNode* node = nodeFromIndex(index);
    if (!node) return false;

    if (role == Qt::EditRole && index.column() == 1) {
        std::string strVal = value.toString().toStdString();

        if (node->value.is_boolean()) {
            node->value = (strVal == "true" || strVal == "1");
        } else if (node->value.is_number_integer()) {
            try {
                node->value = std::stoll(strVal);
            } catch (...) {
                return false;
            }
        } else if (node->value.is_number_float()) {
            try {
                node->value = std::stod(strVal);
            } catch (...) {
                return false;
            }
        } else if (node->value.is_string()) {
            node->value = strVal;
        } else if (node->value.is_null()) {
            // Try to infer type
            if (strVal == "null" || strVal.empty()) {
                node->value = nullptr;
            } else if (strVal == "true") {
                node->value = true;
            } else if (strVal == "false") {
                node->value = false;
            } else {
                try {
                    node->value = std::stoll(strVal);
                } catch (...) {
                    try {
                        node->value = std::stod(strVal);
                    } catch (...) {
                        node->value = strVal;
                    }
                }
            }
        } else {
            return false; // Can't edit objects/arrays directly
        }

        node->updateParentValue();
        emit dataChanged(index, index, {role});
        return true;
    }

    if (role == Qt::CheckStateRole && index.column() == 1 && node->value.is_boolean()) {
        node->value = (value.toInt() == Qt::Checked);
        node->updateParentValue();
        emit dataChanged(index, index, {role});
        return true;
    }

    return false;
}

Qt::ItemFlags JsonModel::flags(const QModelIndex& index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;

    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;

    JsonNode* node = nodeFromIndex(index);
    if (node && index.column() == 1) {
        if (node->value.is_string() || node->value.is_number() ||
            node->value.is_boolean() || node->value.is_null()) {
            f |= Qt::ItemIsEditable;
        }
    }

    if (node && node->value.is_boolean() && index.column() == 1) {
        f |= Qt::ItemIsUserCheckable;
    }

    return f;
}

json JsonModel::getJson() const
{
    if (m_rootNode) return m_rootNode->value;
    return nullptr;
}
