#include "log_vm.h"

#include "../aegisub_core.h"

#include <QClipboard>
#include <QGuiApplication>

LogViewModel::LogViewModel(QObject *parent) : QObject(parent) {
    timer_.setInterval(200);
    connect(&timer_, &QTimer::timeout, this, &LogViewModel::refresh);
    refresh();
}

void LogViewModel::setVisible(bool visible) {
    if (visible) {
        refresh();
        timer_.start();
    } else {
        timer_.stop();
    }
}

void LogViewModel::refresh() {
    if (aegisub_qt::ReadRecentLog(revision_, text_))
        emit textChanged();
}

void LogViewModel::clear() {
    aegisub_qt::ClearRecentLog();
    refresh();
}

void LogViewModel::copy() {
    // Copy the snapshot the user sees, not a potentially newer buffer.
    if (auto *clipboard = QGuiApplication::clipboard())
        clipboard->setText(text_);
}
