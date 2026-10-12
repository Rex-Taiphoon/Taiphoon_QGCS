/**
 * @file toast_model.cpp
 * @brief 提示訊息模型實作
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#include "app/toast_model.hpp"

#include <QTimer>

namespace gcs
{

ToastModel::ToastModel(QObject* parent) : QAbstractListModel(parent)
{
}

int ToastModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(toasts_.size());
}

QHash<int, QByteArray> ToastModel::roleNames() const
{
    return {{IdRole, "toastId"}, {KindRole, "kind"}, {TitleRole, "title"}, {MessageRole, "message"}};
}

QVariant ToastModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= toasts_.size())
    {
        return {};
    }
    const Toast& toast = toasts_.at(index.row());
    switch (role)
    {
    case IdRole:
        return toast.id;
    case KindRole:
        switch (toast.kind)
        {
        case Kind::SUCCESS:
            return QStringLiteral("success");
        case Kind::ERROR:
            return QStringLiteral("error");
        case Kind::WARNING:
            return QStringLiteral("warning");
        }
        return QStringLiteral("warning");
    case TitleRole:
        return toast.title;
    case MessageRole:
        return toast.message;
    default:
        return {};
    }
}

void ToastModel::add(Kind kind, const QString& title, const QString& message)
{
    const int id = next_id_++;
    const int row = static_cast<int>(toasts_.size());
    beginInsertRows(QModelIndex(), row, row);
    toasts_.push_back({id, kind, title, message});
    endInsertRows();
    QTimer::singleShot(AUTO_DISMISS_MS, this, [this, id]() { remove(id); });
}

void ToastModel::remove(int id)
{
    for (int row = 0; row < toasts_.size(); ++row)
    {
        if (toasts_.at(row).id == id)
        {
            beginRemoveRows(QModelIndex(), row, row);
            toasts_.removeAt(row);
            endRemoveRows();
            return;
        }
    }
}

}  // namespace gcs
