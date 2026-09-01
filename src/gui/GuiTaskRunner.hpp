#pragma once

#include <QObject>
#include <QString>
#include <functional>
#include <torrentutils/core/cancellation.hpp>
#include <torrentutils/core/task.hpp>

/** Serial QThread-backed runner for one GUI operation at a time. */
class GuiTaskRunner final : public QObject
{
    Q_OBJECT
  public:
    using Work = std::function<void(const torrentutils::core::CancellationToken&)>;
    using Completion = std::function<void()>;

    explicit GuiTaskRunner(QObject* parent = nullptr);
    ~GuiTaskRunner() override;

    [[nodiscard]] bool is_running() const noexcept;
    bool start(Work work, Completion completion);
    void cancel() noexcept;
    void report(const torrentutils::core::ProgressInfo& progress);

  signals:
    void progress(QString stage, qulonglong completed, qulonglong total, qulonglong completed_bytes,
                  qulonglong total_bytes);
    void finished();
    void failed(QString message);

  private:
    class Worker;

    torrentutils::core::CancellationSource cancellation_;
    Worker* worker_{};
    class QThread* thread_{};
    bool running_{};
};
