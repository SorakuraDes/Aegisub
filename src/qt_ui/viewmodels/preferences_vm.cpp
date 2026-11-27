#include "../../options.h"
#include "preferences_vm.h"
#include "../aegisub_core.h"
#include "../ffmpegsource_common_qt.h"
#include <QFileInfo>
#include <QDir>
#include <QSettings>
#include <algorithm>
#include <cmath>

namespace {
constexpr auto ffmsLogLevelKey = "Provider/FFmpegSource/Log Level";
constexpr int lastPreferencePage = 9;

struct ColorGroup {
    const char *id;
    const char *prefix;
};
constexpr ColorGroup colorGroups[] = {
    {"audio", "Colour/Audio Display/"}, {"syntax", "Colour/Subtitle/Syntax/"},
    {"grid", "Colour/Subtitle Grid/"}, {"visual", "Colour/Visual Tools/"},
};
const ColorGroup *colorGroup(const QString &id) {
    for (const auto &group : colorGroups)
        if (id == QLatin1String(group.id)) return &group;
    return nullptr;
}

QString groupSetting(const QString &id) { return QStringLiteral("ColorOverrides/") + id; }

struct Setting {
    const char *key;
    int min = 0;
    int max = 0;
};
constexpr Setting settings[] = {
    {"App/Save UI State"}, {"Limits/Undo Levels", 2, 10000}, {"Limits/MRU", 0, 16},
    {"App/Auto/Load Linked Files", 0, 2}, {"App/Dark Mode"}, {"Tool/Preferences/Page", 0, lastPreferencePage},
    {"Timing/Default Duration", 0, 36000}, {"Audio/Lock Scroll on Cursor"}, {"Audio/Snap/Enable"},
    {"Audio/Snap/Distance", 0, 25}, {"Audio/Wheel Default to Zoom"}, {"Audio/Medusa Timing Hotkeys"},
    {"Subtitle/Edit Box/Soft Line Break"},
    {"Video/Subtitle Sync"}, {"Video/Open Audio"},
    {"Subtitle/Default Resolution/Auto"}, {"Subtitle/Default Resolution/Width", 1, 32768}, {"Subtitle/Default Resolution/Height", 1, 32768},
    {"Subtitle/Highlight/Syntax"}, {"Subtitle/Edit Box/Font Size", 3, 42}, {"Subtitle/Grid/Font Size", 3, 42},
    {"Subtitle/Grid/Hide Overrides Char"}, {"Subtitle/Character Limit", 0, 1000},
    {"App/Auto/Save"}, {"App/Auto/Save Every Seconds", 1, 2147483}, {"App/Auto/Save on Every Change"},
    {"App/Auto/Backup"}, {"Path/Auto/Save"}, {"Path/Auto/Backup"},
    {ffmsLogLevelKey}, {"Provider/Video/FFmpegSource/Decoding Threads", -1, 256},
    {"Provider/Video/FFmpegSource/Unsafe Seeking"},
    {"Audio/Lead/IN", 0, 36000}, {"Audio/Lead/OUT", 0, 36000},
    {"Video/Slider/Fast Jump Step", 1, 1000},
    {"Subtitle/Edit Box/Font Face"}, {"Subtitle/Grid/Font Face"},
    {"Subtitle/Character Counter/CPS Warning Threshold", 0, 1000},
    {"Subtitle/Character Counter/CPS Error Threshold", 0, 1000},
    {"Subtitle/Character Counter/Ignore Whitespace"}, {"Subtitle/Character Counter/Ignore Punctuation"},
    {"Path/Automation/Include"}, {"Path/Automation/Autoload"}, {"Automation/Trace Level", 0, 5},
    {"Colour/Audio Display/Play Cursor"}, {"Colour/Audio Display/Line boundary Start"},
    {"Colour/Audio Display/Line boundary End"}, {"Colour/Audio Display/Keyframe"},
    {"Colour/Subtitle/Syntax/Normal"}, {"Colour/Subtitle/Syntax/Brackets"},
    {"Colour/Subtitle/Syntax/Tags"}, {"Colour/Subtitle/Syntax/Parameters"},
    {"Colour/Subtitle/Syntax/Error"}, {"Colour/Subtitle/Syntax/Line Break"},
    {"Colour/Subtitle Grid/Standard"}, {"Colour/Subtitle Grid/Selection"},
    {"Colour/Subtitle Grid/Active Border"}, {"Colour/Subtitle Grid/CPS Error"},
    {"Colour/Subtitle Grid/Background/Background"}, {"Colour/Subtitle Grid/Background/Selection"},
    {"Colour/Subtitle Grid/Background/Comment"}, {"Colour/Subtitle Grid/Background/Open Fold"},
    {"Colour/Subtitle Grid/Background/Closed Fold"}, {"Colour/Visual Tools/Lines Primary"},
    {"Colour/Visual Tools/Highlight Primary"}, {"Colour/Visual Tools/Shaded Area Alpha", 0, 1},
};

QVariant readOption(const QString &key, const agi::OptionValue *option) {
    switch (option->GetType()) {
    case agi::OptionType::Bool: return option->GetBool();
    case agi::OptionType::Int: return QVariant::fromValue<qlonglong>(option->GetInt());
    case agi::OptionType::Double: return option->GetDouble();
    case agi::OptionType::Color: {
        const auto &rgb = option->GetColor();
        return QColor(rgb.r, rgb.g, rgb.b);
    }
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
    const auto path = configFilePath();
    if (!path.isEmpty()) {
        const QSettings store(QFileInfo(path).dir().filePath(QStringLiteral("qt-theme.ini")), QSettings::IniFormat);
        for (const auto &group : colorGroups) {
            const QString id = QString::fromLatin1(group.id);
            color_groups_.insert(id, store.value(groupSetting(id), false).toBool());
        }
    }
    for (const auto &setting : settings) {
        auto *opt = OPT_SET(setting.key);
        const auto key = QString::fromLatin1(setting.key);
        values_.insert(key, readOption(key, opt));
        subscriptions_.push_back(opt->Subscribe([this, key, opt] {
            values_.insert(key, readOption(key, opt));
            if (key.startsWith(QLatin1String("Colour/"))) notifyColorsChanged();
            emit optionsChanged();
        }));
    }
}

QVariantMap PreferencesViewModel::optionValues() const { return values_; }
QVariant PreferencesViewModel::option(const QString &key) const { return values_.value(key); }
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
        case agi::OptionType::Double: {
            bool valid = false;
            const double input = value.toDouble(&valid);
            if (!valid || !std::isfinite(input) || input < setting.min || input > setting.max) return false;
            if (opt->GetDouble() == input) return retrySave();
            opt->SetDouble(input);
            break;
        }
        case agi::OptionType::Color: {
            const QColor input = value.metaType() == QMetaType::fromType<QString>()
                ? QColor(value.toString()) : value.value<QColor>();
            if (!input.isValid() || input.alpha() != 255) return false;
            const agi::Color rgb(static_cast<unsigned char>(input.red()), static_cast<unsigned char>(input.green()),
                                 static_cast<unsigned char>(input.blue()));
            if (opt->GetColor() == rgb) return retrySave();
            opt->SetColor(rgb);
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
    const bool optionsSaved = options_error_.isEmpty() || flushChanges();
    const bool colorsSaved = !color_groups_dirty_ || flushColorGroups();
    return optionsSaved && colorsSaved;
}

bool PreferencesViewModel::flushChanges() {
    // Keep live values on failure; the next flush writes all pending changes together.
    QString error;
    const bool saved = aegisub_qt::FlushOptions(&error);
    options_error_ = saved ? QString() : error.isEmpty() ? tr("Could not write the configuration file.") : error;
    updatePersistenceError();
    return saved;
}

void PreferencesViewModel::updatePersistenceError() {
    const QString error = options_error_.isEmpty() ? color_groups_error_
        : color_groups_error_.isEmpty() ? options_error_ : options_error_ + u'\n' + color_groups_error_;
    if (persistence_error_ == error) return;
    persistence_error_ = error;
    emit persistenceErrorChanged();
}

bool PreferencesViewModel::flushColorGroups() {
    const auto path = configFilePath();
    if (!path.isEmpty()) {
        QSettings store(QFileInfo(path).dir().filePath(QStringLiteral("qt-theme.ini")), QSettings::IniFormat);
        for (auto it = color_groups_.cbegin(); it != color_groups_.cend(); ++it)
            store.setValue(groupSetting(it.key()), it.value());
        store.sync();
        color_groups_dirty_ = store.status() != QSettings::NoError;
    }
    color_groups_error_ = color_groups_dirty_ ? tr("Could not save color override modes.") : QString();
    updatePersistenceError();
    return !color_groups_dirty_;
}

void PreferencesViewModel::notifyColorsChanged() {
    ++color_revision_;
    emit colorsChanged();
}

bool PreferencesViewModel::colorOverridesEnabled(const QString &group) const {
    return color_groups_.value(group).toBool();
}

bool PreferencesViewModel::setColorOverridesEnabled(const QString &group, bool enabled) {
    if (!config::opt || !colorGroup(group)) return false;
    if (colorOverridesEnabled(group) == enabled) return retrySave();
    color_groups_.insert(group, enabled);
    color_groups_dirty_ = true;
    notifyColorsChanged();
    return flushColorGroups();
}

QVariantMap PreferencesViewModel::groupColors(const QString &group) const {
    QVariantMap colors;
    const auto *info = colorGroup(group);
    if (!info || !colorOverridesEnabled(group)) return colors;
    const QString prefix = QString::fromLatin1(info->prefix);
    for (auto it = values_.lowerBound(prefix); it != values_.cend() && it.key().startsWith(prefix); ++it)
        colors.insert(it.key().mid(prefix.size()), it.value());
    return colors;
}

QVariantMap PreferencesViewModel::audioColors() const { return groupColors(QStringLiteral("audio")); }
QVariantMap PreferencesViewModel::syntaxColors() const { return groupColors(QStringLiteral("syntax")); }
QVariantMap PreferencesViewModel::gridColors() const { return groupColors(QStringLiteral("grid")); }
QVariantMap PreferencesViewModel::visualColors() const { return groupColors(QStringLiteral("visual")); }

QColor PreferencesViewModel::effectiveColor(const QString &group, const QString &key, const QColor &fallback) const {
    const auto *info = colorGroup(group);
    if (!info || !colorOverridesEnabled(group)) return fallback;
    const auto color = option(QString::fromLatin1(info->prefix) + key).value<QColor>();
    return color.isValid() ? color : fallback;
}

bool PreferencesViewModel::setColorOverride(const QString &group, const QString &key, const QColor &color) {
    const auto *info = colorGroup(group);
    if (!info) return false;
    const QString fullKey = QString::fromLatin1(info->prefix) + key;
    if (option(fullKey).metaType() != QMetaType::fromType<QColor>()) return false;
    return setOption(fullKey, color);
}

bool PreferencesViewModel::clearColorOverride(const QString &group, const QString &key) {
    const auto *info = colorGroup(group);
    if (!config::opt || !info) return false;
    const QString fullKey = QString::fromLatin1(info->prefix) + key;
    if (!values_.contains(fullKey)) return false;
    auto *opt = config::opt->Get(fullKey.toStdString());
    if (opt->IsDefault()) return retrySave();
    opt->Reset();
    return flushChanges();
}

bool PreferencesViewModel::autoSave() const { return option("App/Auto/Save").toBool(); }
void PreferencesViewModel::setAutoSave(bool value) { setOption(QStringLiteral("App/Auto/Save"), value); }
bool PreferencesViewModel::syntaxHighlighting() const { return option("Subtitle/Highlight/Syntax").toBool(); }
int PreferencesViewModel::editFontSize() const { return std::clamp(option("Subtitle/Edit Box/Font Size").toInt(), 3, 42); }
int PreferencesViewModel::gridFontSize() const { return std::clamp(option("Subtitle/Grid/Font Size").toInt(), 3, 42); }
QString PreferencesViewModel::hideOverridesChar() const { return option("Subtitle/Grid/Hide Overrides Char").toString(); }
int PreferencesViewModel::characterLimit() const { return option("Subtitle/Character Limit").toInt(); }
QString PreferencesViewModel::editFontFace() const { return option("Subtitle/Edit Box/Font Face").toString(); }
QString PreferencesViewModel::gridFontFace() const { return option("Subtitle/Grid/Font Face").toString(); }
int PreferencesViewModel::cpsWarningThreshold() const { return option("Subtitle/Character Counter/CPS Warning Threshold").toInt(); }
int PreferencesViewModel::cpsErrorThreshold() const { return option("Subtitle/Character Counter/CPS Error Threshold").toInt(); }
bool PreferencesViewModel::ignoreWhitespace() const { return option("Subtitle/Character Counter/Ignore Whitespace").toBool(); }
bool PreferencesViewModel::ignorePunctuation() const { return option("Subtitle/Character Counter/Ignore Punctuation").toBool(); }
int PreferencesViewModel::preferencePage() const { return std::clamp(option("Tool/Preferences/Page").toInt(), 0, lastPreferencePage); }
void PreferencesViewModel::setPreferencePage(int value) { setOption(QStringLiteral("Tool/Preferences/Page"), value); }
QString PreferencesViewModel::configFilePath() const { return aegisub_qt::OptionsFilePath(); }
