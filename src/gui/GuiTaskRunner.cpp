#include "GuiTaskRunner.hpp"

#include <QThread>

class GuiTaskRunner::Worker final : public QObject
{
    Q_OBJECT
  public:
    Worker(Work work, torrentutils::core::CancellationToken token)
        : work_(std::move(work)), token_(std::move(token))
    {
    }

  public slots:
    void execute()
    {
        try
        {
            work_(token_);
            emit completed();
        }
        catch (const std::exception& error)
        {
            emit failed(QString::fromUtf8(error.what()));
        }
        catch (...)
        {
            emit failed(QStringLiteral("The background operation failed unexpectedly."));
        }
    }

  signals:
    void completed();
    void failed(QString message);

  private:
    Work work_;
    torrentutils::core::CancellationToken token_;
};

GuiTaskRunner::GuiTaskRunner(QObject* parent) : QObject(parent) {}

GuiTaskRunner::~GuiTaskRunner()
{
    cancel();
    if (thread_ != nullptr && thread_->isRunning())
    {
        thread_->quit();
        thread_->wait();
    }
}

bool GuiTaskRunner::is_running() const noexcept
{
    return running_;
}

bool GuiTaskRunner::start(Work work, Completion completion)
{
    if (running_ || !work)
    {
        return false;
    }

    cancellation_ = torrentutils::core::CancellationSource{};
    auto* thread = new QThread(this);
    auto* worker = new Worker(std::move(work), cancellation_.token());
    worker->moveToThread(thread);
    worker_ = worker;
    thread_ = thread;
    running_ = true;

    connect(thread, &QThread::started, worker, &Worker::execute);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this, thread] {
        if (thread_ == thread)
            thread_ = nullptr;
    });
    connect(
        worker, &Worker::completed, this,
        [this, thread, completion = std::move(completion)]() mutable {
            running_ = false;
            worker_ = nullptr;
            emit finished();
            if (completion)
            {
                completion();
            }
            thread->quit();
        },
        Qt::QueuedConnection);
    connect(
        worker, &Worker::failed, this,
        [this, thread](const QString& message) {
            running_ = false;
            worker_ = nullptr;
            emit finished();
            emit failed(message);
            thread->quit();
        },
        Qt::QueuedConnection);
    thread->start();
    return true;
}

void GuiTaskRunner::cancel() noexcept
{
    if (running_)
    {
        cancellation_.cancel();
    }
}

void GuiTaskRunner::report(const torrentutils::core::ProgressInfo& progress_info)
{
    emit progress(QString::fromStdString(progress_info.stage),
                  static_cast<qulonglong>(progress_info.completed),
                  static_cast<qulonglong>(progress_info.total),
                  static_cast<qulonglong>(progress_info.completed_bytes),
                  static_cast<qulonglong>(progress_info.total_bytes));
}

#include "GuiTaskRunner.moc"
