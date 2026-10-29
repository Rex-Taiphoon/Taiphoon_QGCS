/**
 * @file log_file.cpp
 * @brief 日誌檔實作
 * @author Taiphoon GCS Team
 * @date 2026-10-15
 */
#include "app/log_file.hpp"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>

namespace gcs::log_file
{

namespace
{

/// 日誌檔名前綴
const QString FILE_PREFIX = QStringLiteral("Taiphoon_QGCS_");

/// 日誌檔狀態；訊息處理器為自由函式，只能以全域狀態保存
struct LogState
{
    QMutex mutex;
    QString directory;
    QDate file_date;
    QFile file;
    QtMessageHandler previous_handler = nullptr;
    bool is_installed = false;
};

LogState g_state;

/**
 * @brief 訊息等級文字
 * @param type 訊息等級
 * @return 固定寬度的等級名稱
 */
QString level_name(QtMsgType type)
{
    switch (type)
    {
    case QtInfoMsg:
        return QStringLiteral("INFO ");
    case QtWarningMsg:
        return QStringLiteral("WARN ");
    case QtCriticalMsg:
        return QStringLiteral("ERROR");
    case QtFatalMsg:
        return QStringLiteral("FATAL");
    default:
        return QStringLiteral("DEBUG");
    }
}

/**
 * @brief 確保已開啟今日的日誌檔（跨日時換檔）；呼叫端需持有 g_state.mutex
 * @return 檔案可寫入
 */
bool ensure_file_open()
{
    const QDate today = QDate::currentDate();
    if (g_state.file.isOpen() && g_state.file_date == today)
    {
        return true;
    }
    g_state.file.close();
    g_state.file.setFileName(QDir(g_state.directory)
                                 .filePath(FILE_PREFIX + today.toString(QStringLiteral("yyyyMMdd"))
                                           + QStringLiteral(".log")));
    g_state.file_date = today;
    return g_state.file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}

/**
 * @brief Qt 訊息處理器：info 以上寫入日誌檔，再轉交原本的處理器
 * @param type 訊息等級
 * @param context 來源資訊（category）
 * @param message 訊息內容
 */
void handle_message(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    QtMessageHandler previous = nullptr;
    {
        QMutexLocker locker(&g_state.mutex);
        previous = g_state.previous_handler;
        if (type != QtDebugMsg && g_state.is_installed && ensure_file_open())
        {
            // 時間以 UTC 記錄，與飛控日誌對照（狀態列時鐘為 UTC+8）
            const QString line = QStringLiteral("%1 [%2] %3: %4\n")
                                     .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs),
                                          level_name(type),
                                          QString::fromUtf8(context.category != nullptr ? context.category
                                                                                         : "default"),
                                          message);
            g_state.file.write(line.toUtf8());
            g_state.file.flush();
        }
    }
    if (previous != nullptr)
    {
        previous(type, context, message);
    }
}

}  // namespace

bool install(const QString& directory)
{
    if (!QDir().mkpath(directory))
    {
        return false;
    }
    QMutexLocker locker(&g_state.mutex);
    g_state.file.close();
    g_state.directory = directory;
    if (!g_state.is_installed)
    {
        g_state.is_installed = true;
        // 在鎖內安裝：qInstallMessageHandler 不會輸出訊息，不會重入 handle_message
        g_state.previous_handler = qInstallMessageHandler(handle_message);
    }
    return true;
}

void uninstall()
{
    QMutexLocker locker(&g_state.mutex);
    if (!g_state.is_installed)
    {
        return;
    }
    qInstallMessageHandler(g_state.previous_handler);
    g_state.previous_handler = nullptr;
    g_state.is_installed = false;
    g_state.file.close();
}

QString current_path()
{
    QMutexLocker locker(&g_state.mutex);
    return g_state.file.isOpen() ? g_state.file.fileName() : QString();
}

}  // namespace gcs::log_file
