#include "../../ass_file.h"
#include "../../options.h"
#include <libaegisub/path.h>

#include "fonts_collector_vm.h"
#include "document_vm.h"
#include "../fonts_collector_bridge.h"

#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <algorithm>
#include <memory>

namespace {
constexpr auto actionKey = "Tool/Fonts Collector/Action";
constexpr auto destinationKey = "Path/Fonts Collector Destination";
}

FontsCollectorViewModel::FontsCollectorViewModel(AssFile *file, DocumentViewModel *document, QObject *parent)
: QObject(parent), file_(file), document_(document) {
    if (config::opt) {
        action_ = static_cast<int>(std::clamp<int64_t>(OPT_GET(actionKey)->GetInt(), 0, 3));
        destination_ = QString::fromStdString(OPT_GET(destinationKey)->GetString());
    }
    connect(document_, &DocumentViewModel::filePathChanged, this, &FontsCollectorViewModel::hasScriptPathChanged);
}

FontsCollectorViewModel::~FontsCollectorViewModel() {
    if (worker_.joinable()) worker_.join();
}

bool FontsCollectorViewModel::hasScriptPath() const { return !document_->filePath().isEmpty(); }

void FontsCollectorViewModel::setAction(int action) {
    if (running_ || action < 0 || action > 3 || action_ == action || (action == 2 && !hasScriptPath())) return;
    action_ = action;
    if (config::opt) OPT_SET(actionKey)->SetInt(action_);
    emit actionChanged();
}

void FontsCollectorViewModel::setDestination(const QString &destination) {
    if (running_ || destination_ == destination) return;
    destination_ = destination;
    if (config::opt) OPT_SET(destinationKey)->SetString(destination_.toStdString());
    emit destinationChanged();
}

QString FontsCollectorViewModel::resolve(const QString &path) const {
    auto input = QDir::fromNativeSeparators(path.trimmed());
    if (input.isEmpty()) return {};
    const auto token = input.section(QLatin1Char('/'), 0, 0);
    if (token == QLatin1String("?script") || token == QLatin1String("?user") || token == QLatin1String("?data")) {
        QString base;
        if (token == QLatin1String("?script")) {
            if (hasScriptPath()) base = QFileInfo(document_->filePath()).absolutePath();
        } else if (config::path) {
            const auto decoded = config::path->Decode(token.toStdString());
            base = QString::fromStdWString(decoded.wstring());
            if (base == token) base.clear();
        }
        if (base.isEmpty()) return {};
        input = input.size() == token.size() ? base : QDir(base).filePath(input.mid(token.size() + 1));
    }
    return QDir::cleanPath(QFileInfo(input).absoluteFilePath());
}

QUrl FontsCollectorViewModel::browseFolder() const {
    auto resolved = resolve(destination_);
    if (resolved.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive)) resolved = QFileInfo(resolved).absolutePath();
    if (!QFileInfo(resolved).isDir()) resolved = resolve(QStringLiteral("?script"));
    if (resolved.isEmpty()) resolved = resolve(QStringLiteral("?user"));
    return QUrl::fromLocalFile(resolved);
}

void FontsCollectorViewModel::selectDestination(const QUrl &url) {
    if (url.isLocalFile()) setDestination(url.toLocalFile());
}

void FontsCollectorViewModel::copyLog() const { QGuiApplication::clipboard()->setText(log_); }

void FontsCollectorViewModel::finish(int collected, int missing, bool failed) {
    if (worker_.joinable()) worker_.join();
    collected_ = collected;
    missing_ = missing;
    failed_ = failed;
    status_ = failed ? QStringLiteral("Font collection failed")
                     : QStringLiteral("Font files found: %1; missing: %2").arg(collected).arg(missing);
    emit statusChanged();
    if (running_) {
        running_ = false;
        emit runningChanged();
    }
}

void FontsCollectorViewModel::start() {
    if (running_) return;
    log_.clear();
    emit logChanged();
    try {
        const auto action = action_;
        auto target = action ? resolve(action == 2 ? QStringLiteral("?script") : destination_) : QString{};
        if (action && target.isEmpty()) {
            log_ = QStringLiteral("* Invalid destination or unavailable path token.\n");
            emit logChanged();
            finish(0, 0, true);
            return;
        }
        if (action == 3 && !target.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive))
            target = QDir(target).filePath(QStringLiteral("fonts.zip"));
        if (action == 1 && target.endsWith(QLatin1String(".zip"), Qt::CaseInsensitive) && !QFileInfo(target).isDir())
            target = QFileInfo(target).absolutePath();
        auto snapshot = std::make_unique<AssFile>(*file_);
        snapshot->Properties = file_->Properties;
        const auto native = std::filesystem::path(QDir::toNativeSeparators(target).toStdWString());
        running_ = true;
        status_ = QStringLiteral("Collecting fonts...");
        collected_ = missing_ = 0;
        failed_ = false;
        emit runningChanged();
        emit statusChanged();
        worker_ = std::thread([this, snapshot = std::move(snapshot), action, native] {
            auto log = [this](std::string text) {
                QMetaObject::invokeMethod(this, [this, text = QString::fromStdString(text)] {
                    log_ += text;
                    emit logChanged();
                }, Qt::QueuedConnection);
            };
            aegisub_qt::FontsCollectorResult result;
            try {
                result = aegisub_qt::CollectFonts(*snapshot, action, native, log);
            } catch (const agi::Exception &err) {
                result.outputFailed = true;
                log("* Font collection failed: " + err.GetMessage() + "\n");
            } catch (...) {
                result.outputFailed = true;
                log("* Unexpected error during font collection.\n");
            }
            QMetaObject::invokeMethod(this, [this, count = int(result.paths.size()), missing = result.missingCount,
                                             failed = result.outputFailed] { finish(count, missing, failed); }, Qt::QueuedConnection);
        });
    } catch (...) {
        log_ = QStringLiteral("* Could not start the font collection worker.\n");
        emit logChanged();
        finish(0, 0, true);
    }
}
