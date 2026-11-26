#pragma once

#include <QVariantMap>
#include <QStringList>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <utility>

namespace aegisub_qt::automation {

// Poll at a bounded rate so a tight Lua reporting loop cannot flood the GUI queue.
struct AutomationJob {
    enum class Kind { Form, File, Gui };
    struct Request {
        Kind kind;
        QVariantMap data;
        std::function<void()> gui;
    };
    std::atomic_bool cancelled{false};
    std::atomic_bool done{false};
    std::mutex mutex;
    std::condition_variable changed;
    std::optional<Request> pending;
    std::optional<QVariantMap> answer;
    QString title;
    QString task;
    QList<QPair<int, QString>> log;
    double progress = 0;

    QVariantMap ask(Request request) {
        std::unique_lock lock(mutex);
        if (cancelled.load()) return {};
        answer.reset();
        pending = std::move(request);
        changed.wait(lock, [&] { return answer.has_value() || cancelled.load(); });
        return answer.value_or(QVariantMap{});
    }

    void reply(QVariantMap value) {
        {
            std::lock_guard lock(mutex);
            answer = std::move(value);
        }
        changed.notify_one();
    }

    void cancel() {
        {
            std::lock_guard lock(mutex);
            cancelled.store(true);
            pending.reset();
        }
        changed.notify_one();
    }
};

} // namespace aegisub_qt::automation
