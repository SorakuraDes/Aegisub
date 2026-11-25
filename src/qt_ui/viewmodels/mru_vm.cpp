#include "mru_vm.h"
#include "../aegisub_core.h"
#include "../../options.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <algorithm>

namespace {
const QString keys[] = {QStringLiteral("Subtitle"), QStringLiteral("Video"), QStringLiteral("Audio")};
QJsonObject readLists(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject{};
}
}

MruViewModel::MruViewModel(QObject *parent) : QObject(parent) {
    const auto options = aegisub_qt::OptionsFilePath();
    if (!options.isEmpty()) {
        filePath_ = QFileInfo(options).dir().filePath(QStringLiteral("mru.json"));
        const auto entries = readLists(filePath_);
        for (int i = 0; i < 3; ++i)
            for (const auto &value : entries.value(keys[i]).toArray())
                if (value.isString() && !value.toString().isEmpty())
                    lists_[i].append(value.toString());
    }
    if (config::opt) {
        limit_ = int(std::clamp<int64_t>(OPT_GET("Limits/MRU")->GetInt(), 0, 16));
        limitConnection_ = OPT_SUB("Limits/MRU", [this] { applyLimit(); });
    }
    for (auto &list : lists_) {
        list.removeDuplicates();
        while (list.size() > limit_) list.removeLast();
    }
}

void MruViewModel::applyLimit() {
    limit_ = int(std::clamp<int64_t>(OPT_GET("Limits/MRU")->GetInt(), 0, 16));
    for (auto &list : lists_)
        while (list.size() > limit_) list.removeLast();
    save();
    emit changed();
}

void MruViewModel::record(int kind, const QString &path) {
    if (limit_ == 0 || path.isEmpty()) return;
    const auto absolute = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    auto &list = lists_[kind];
    if (!list.isEmpty() && list.first() == absolute) return;
    list.removeAll(absolute);
    list.prepend(absolute);
    while (list.size() > limit_) list.removeLast();
    save();
    emit changed();
}

void MruViewModel::save() const {
    if (filePath_.isEmpty()) return;
    auto entries = readLists(filePath_);
    for (int i = 0; i < 3; ++i)
        entries.insert(keys[i], QJsonArray::fromStringList(lists_[i]));
    QSaveFile file(filePath_);
    const auto json = QJsonDocument(entries).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size() || !file.commit())
        qWarning("Could not write MRU: %s", qPrintable(file.errorString()));
}
