/**
 * @file voice_controller.cpp
 * @brief 按住說話實作
 * @author Taiphoon GCS Team
 * @date 2026-10-26
 */
#include "app/voice_controller.hpp"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QLoggingCategory>
#include <QVector>

#if defined(GCS_HAS_QT_MULTIMEDIA)
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioSource>
#include <QMediaDevices>
#include <QPermissions>
#endif

#include <cmath>

namespace
{
Q_LOGGING_CATEGORY(lc_voice_ui, "gcs.ui.voice")
}  // namespace

namespace gcs
{

namespace
{

/// 希望的收音格式：與模型相同的 16 kHz 單聲道；裝置不支援時改用裝置偏好格式，由 sherpa-onnx 重新取樣
constexpr int PREFERRED_SAMPLE_RATE = 16000;

}  // namespace

#if defined(GCS_HAS_QT_MULTIMEDIA)

/// 收音中的麥克風
struct VoiceController::Capture
{
    std::unique_ptr<QAudioSource> source;
    /// 由 source 擁有，stop 後失效
    QIODevice* device = nullptr;
    QAudioFormat format;
};

#else

/// 未包含 Qt Multimedia 的版本沒有收音
struct VoiceController::Capture
{
};

#endif

VoiceController::VoiceController(QObject* parent) : QObject(parent), listen_limit_(this)
{
    listen_limit_.setSingleShot(true);
    listen_limit_.setInterval(MAX_LISTEN_MS);
    connect(&listen_limit_, &QTimer::timeout, this, &VoiceController::endListening);
}

VoiceController::~VoiceController() = default;

bool VoiceController::is_listening() const
{
    return capture_ != nullptr;
}

void VoiceController::set_available(bool is_available, const QString& reason)
{
    is_available_ = is_available;
    unavailable_reason_ = reason;
    if (!is_available)
    {
        qCWarning(lc_voice_ui).noquote() << "語音口令停用：" << reason;
    }
    emit availabilityChanged();
}

void VoiceController::set_result(const QString& text)
{
    last_result_ = text;
    emit resultChanged();
}

void VoiceController::initialize(const QString& voice_dir)
{
#if defined(GCS_HAS_QT_MULTIMEDIA)
    QString error;
    if (!spotter_.load(voice_dir, &error))
    {
        set_available(false, error);
        return;
    }
    // macOS 未取得權限就開啟麥克風會被系統終止；Windows 一律回傳已允許
    const QMicrophonePermission permission;
    switch (qApp->checkPermission(permission))
    {
    case Qt::PermissionStatus::Granted:
        set_available(true, QString());
        break;
    case Qt::PermissionStatus::Denied:
        set_available(false, QStringLiteral("未取得麥克風權限"));
        break;
    case Qt::PermissionStatus::Undetermined:
        qApp->requestPermission(permission, this,
                                [this](const QPermission& result)
                                {
                                    const bool is_granted = result.status() == Qt::PermissionStatus::Granted;
                                    set_available(is_granted,
                                                  is_granted ? QString() : QStringLiteral("未取得麥克風權限"));
                                });
        break;
    }
#else
    Q_UNUSED(voice_dir)
    set_available(false, QStringLiteral("此版本未包含麥克風支援（Qt Multimedia）"));
#endif
}

void VoiceController::beginListening()
{
#if defined(GCS_HAS_QT_MULTIMEDIA)
    if (!is_available_ || capture_ != nullptr)
    {
        return;
    }
    // 每次按下才取預設裝置：遠端桌面重新連線或插拔耳麥後裝置會改變
    const QAudioDevice device = QMediaDevices::defaultAudioInput();
    if (device.isNull())
    {
        set_result(QStringLiteral("找不到麥克風"));
        return;
    }
    auto capture = std::make_unique<Capture>();
    capture->format.setSampleRate(PREFERRED_SAMPLE_RATE);
    capture->format.setChannelCount(1);
    capture->format.setSampleFormat(QAudioFormat::Float);
    if (!device.isFormatSupported(capture->format))
    {
        capture->format = device.preferredFormat();
    }
    capture->source = std::make_unique<QAudioSource>(device, capture->format);
    capture->device = capture->source->start();
    if (capture->device == nullptr)
    {
        set_result(QStringLiteral("無法開啟麥克風"));
        return;
    }
    connect(capture->device, &QIODevice::readyRead, this, &VoiceController::on_audio_ready);
    capture_ = std::move(capture);
    found_.clear();
    spotter_.begin();
    listen_limit_.start();
    qCInfo(lc_voice_ui).noquote() << "開始收音：" << device.description();
    set_result(QStringLiteral("聆聽中"));
    emit listeningChanged();
#endif
}

void VoiceController::endListening()
{
#if defined(GCS_HAS_QT_MULTIMEDIA)
    if (capture_ == nullptr)
    {
        return;
    }
    listen_limit_.stop();
    on_audio_ready();
    const int sample_rate = capture_->format.sampleRate();
    capture_->source->stop();
    capture_.reset();
    found_ += spotter_.finish(sample_rate);
    level_ = 0.0;
    emit levelChanged();
    emit listeningChanged();

    const QStringList found = found_;
    found_.clear();
    set_result(found.isEmpty() ? QStringLiteral("未辨識到口令")
                               : QStringLiteral("辨識到：") + found.join(QStringLiteral("、")));
    qCInfo(lc_voice_ui).noquote() << last_result_;
    for (const QString& keyword : found)
    {
        emit keyword_detected(keyword);
    }
#endif
}

void VoiceController::on_audio_ready()
{
#if defined(GCS_HAS_QT_MULTIMEDIA)
    if (capture_ == nullptr || capture_->device == nullptr)
    {
        return;
    }
    const QAudioFormat& format = capture_->format;
    const int bytes_per_frame = format.bytesPerFrame();
    const int bytes_per_sample = format.bytesPerSample();
    const int channels = format.channelCount();
    if (bytes_per_frame <= 0 || bytes_per_sample <= 0 || channels <= 0)
    {
        return;
    }
    // 只讀完整的音框，剩餘位元組留到下次
    const qint64 available = capture_->device->bytesAvailable();
    const QByteArray data = capture_->device->read(available - available % bytes_per_frame);
    const int frames = static_cast<int>(data.size() / bytes_per_frame);
    if (frames <= 0)
    {
        return;
    }
    QVector<float> mono(frames);
    double sum_of_squares = 0.0;
    for (int frame = 0; frame < frames; ++frame)
    {
        float sum = 0.0F;
        for (int channel = 0; channel < channels; ++channel)
        {
            sum += format.normalizedSampleValue(data.constData() + frame * bytes_per_frame
                                                + channel * bytes_per_sample);
        }
        mono[frame] = sum / static_cast<float>(channels);
        sum_of_squares += static_cast<double>(mono[frame]) * mono[frame];
    }
    level_ = std::sqrt(sum_of_squares / frames);
    emit levelChanged();
    found_ += spotter_.accept(mono.constData(), frames, format.sampleRate());
#endif
}

bool VoiceController::eventFilter(QObject* watched, QEvent* event)
{
    if (event == nullptr)
    {
        return false;
    }
    const QEvent::Type type = event->type();
    if (type == QEvent::KeyPress || type == QEvent::KeyRelease)
    {
        const auto* key_event = static_cast<QKeyEvent*>(event);
        if (key_event->key() == PUSH_TO_TALK_KEY)
        {
            if (!key_event->isAutoRepeat())
            {
                if (type == QEvent::KeyPress)
                {
                    beginListening();
                }
                else
                {
                    endListening();
                }
            }
            return true;
        }
    }
    // 按住時切到其他程式，放開事件不會送來
    if (type == QEvent::ApplicationDeactivate)
    {
        endListening();
    }
    return QObject::eventFilter(watched, event);
}

}  // namespace gcs
