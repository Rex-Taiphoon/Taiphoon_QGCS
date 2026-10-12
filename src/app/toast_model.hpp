/**
 * @file toast_model.hpp
 * @brief 可堆疊的提示訊息（網頁版 toastStore），6 秒後自動移除
 * @author Taiphoon GCS Team
 * @date 2026-10-12
 */
#pragma once

#include <QAbstractListModel>
#include <QVector>

namespace gcs
{

/**
 * @class ToastModel
 * @brief 提示訊息清單
 */
class ToastModel : public QAbstractListModel
{
    Q_OBJECT

public:
    /// 自動移除時間（毫秒）
    static constexpr int AUTO_DISMISS_MS = 6000;

    /// 提示種類
    enum class Kind
    {
        SUCCESS,
        ERROR,
        WARNING
    };

    enum Role
    {
        IdRole = Qt::UserRole + 1,
        KindRole,
        TitleRole,
        MessageRole
    };

    explicit ToastModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /**
     * @brief 新增提示
     * @param kind 種類
     * @param title 標題
     * @param message 內容
     */
    void add(Kind kind, const QString& title, const QString& message);

    /** @brief 移除指定提示 @param id 提示識別碼 */
    Q_INVOKABLE void remove(int id);

private:
    struct Toast
    {
        int id = 0;
        Kind kind = Kind::WARNING;
        QString title;
        QString message;
    };

    QVector<Toast> toasts_;
    int next_id_ = 1;
};

}  // namespace gcs
