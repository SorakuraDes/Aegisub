#include "../../options.h"
#include "preferences_vm.h"
#include "../aegisub_core.h"
#include "../ffmpegsource_common_qt.h"
#include <algorithm>

namespace {
constexpr auto ffmsLogLevelKey = "Provider/FFmpegSource/Log Level";

struct Setting {
    const char *key;
    int min = 0;
    int max = 0;
};
constexpr Setting settings[] = {
    {"App/Save UI State"}, {"Limits/Undo Levels", 2, 10000}, {"Limits/MRU", 0, 16},
    {"App/Auto/Load Linked Files", 0, 2}, {"App/Dark Mode"}, {"Tool/Preferences/Page", 0, 6},
    {"Timing/Default Duration", 0, 36000}, {"Audio/Lock Scroll on Cursor"}, {"Audio/Snap/Enable"},
    {"Audio/Snap/Distance", 0, 25}, {"Audio/Wheel Default to Zoom"},
    {"Video/Subtitle Sync"}, {"Video/Open Audio"},
    {"Subtitle/Default Resolution/Auto"}, {"Subtitle/Default Resolution/Width", 1, 32768}, {"Subtitle/Default Resolution/Height", 1, 32768},
    {"Subtitle/Highlight/Syntax"}, {"Subtitle/Edit Box/Font Size", 3, 42}, {"Subtitle/Grid/Font Size", 3, 42},
    {"Subtitle/Grid/Hide Overrides Char"}, {"Subtitle/Character Limit", 0, 1000},
    {"App/Auto/Save"}, {"App/Auto/Save Every Seconds", 1, 2147483}, {"App/Auto/Save on Every Change"},
    {"App/Auto/Backup"}, {"Path/Auto/Save"}, {"Path/Auto/Backup"},
    {ffmsLogLevelKey}, {"Provider/Video/FFmpegSource/Decoding Threads", -1, 256},
    {"Provider/Video/FFmpegSource/Unsafe Seeking"},
};

QVariant readOption(const QString &key, const agi::OptionValue *option) {
    switch (option->GetType()) {
    case agi::OptionType::Bool: return option->GetBool();
    case agi::OptionType::Int: return QVariant::fromValue<qlonglong>(option->GetInt());
    case agi::OptionType::String:
        if (key == QLatin1String(ffmsLogLevelKey)) {
            const auto level = aegisub_qt::NormalizeFfmsLogLevel(option->GetString());
            return level.empty() ? QStringLiteral("quiet") : QString::fromStdString(level);
        }
        return QString::fromStdString(option->GetString());
    default: return {};
    }
}
} // namespace

PreferencesViewModel::PreferencesViewModel(QObject *parent) : QObject(parent) {
    if (!config::opt) return;
    for (const auto &setting : settings) {
        auto *opt = OPT_SET(setting.key);
        const auto key = QString::fromLatin1(setting.key);
        values_.insert(key, readOption(key, opt));
        subscriptions_.push_back(opt->Subscribe([this, key, opt] {
            values_.insert(key, readOption(key, opt));
            emit optionsChanged();
        }));
    }
}

QVariantMap PreferencesViewModel::optionValues() const { return values_; }
QVariant PreferencesViewModel::option(const char *key) const { return values_.value(QString::fromLatin1(key)); }

bool PreferencesViewModel::setOption(const QString &key, const QVariant &value) {
    if (!config::opt || !values_.contains(key)) return false;
    for (const auto &setting : settings) {
        if (key != QLatin1String(setting.key)) continue;
        auto *opt = OPT_SET(setting.key);
        switch (opt->GetType()) {
        case agi::OptionType::Bool:
            if (opt->GetBool() == value.toBool()) return retrySave();
            opt->SetBool(value.toBool());
            break;
        case agi::OptionType::Int: {
            bool valid = false;
            const auto input = value.toLongLong(&valid);
            if (!valid || input < setting.min || input > setting.max) return false;
            if (opt->GetInt() == input) return retrySave();
            opt->SetInt(input);
            break;
        }
        case agi::OptionType::String: {
            auto input = value.toString().toStdString();
            if (key == QLatin1String(ffmsLogLevelKey)) {
                input = aegisub_qt::NormalizeFfmsLogLevel(input);
                if (input.empty()) return false;
            }
            if (opt->GetString() == input) return retrySave();
            opt->SetString(input);
            break;
        }
        default: return false;
        }
        return flushChanges();
    }
    return false;
}

QString PreferencesViewModel::persistenceError() const { return persistence_error_; }

bool PreferencesViewModel::retrySave() {
    return persistence_error_.isEmpty() || flushChanges();
}

bool PreferencesViewModel::flushChanges() {
    // Keep live values on failure; the next flush writes all pending changes together.
    QString error;
    const bool saved = aegisub_qt::FlushOptions(&error);
    if (saved) error.clear();
    else if (error.isEmpty()) error = tr("Could not write the configuration file.");
    if (persistence_error_ != error) {
        persistence_error_ = error;
        emit persistenceErrorChanged();
    }
    return saved;
}

bool PreferencesViewModel::autoSave() const { return option("App/Auto/Save").toBool(); }
void PreferencesViewModel::setAutoSave(bool value) { setOption(QStringLiteral("App/Auto/Save"), value); }
bool PreferencesViewModel::syntaxHighlighting() const { return option("Subtitle/Highlight/Syntax").toBool(); }
int PreferencesViewModel::editFontSize() const { return std::clamp(option("Subtitle/Edit Box/Font Size").toInt(), 3, 42); }
int PreferencesViewModel::gridFontSize() const { return std::clamp(option("Subtitle/Grid/Font Size").toInt(), 3, 42); }
QString PreferencesViewModel::hideOverridesChar() const { return option("Subtitle/Grid/Hide Overrides Char").toString(); }
int PreferencesViewModel::characterLimit() const { return option("Subtitle/Character Limit").toInt(); }
int PreferencesViewModel::preferencePage() const { return std::clamp(option("Tool/Preferences/Page").toInt(), 0, 6); }
void PreferencesViewModel::setPreferencePage(int value) { setOption(QStringLiteral("Tool/Preferences/Page"), value); }
QString PreferencesViewModel::configFilePath() const { return aegisub_qt::OptionsFilePath(); }
