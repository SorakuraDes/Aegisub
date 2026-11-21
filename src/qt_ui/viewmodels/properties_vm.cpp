// Must come before any Qt header: agi::signal (via ass_file.h) has members
// named `slots`, and Qt defines `signals`/`slots` as macros.
#include "../../ass_file.h"

#include "properties_vm.h"

#include "../commit_bridge.h"
#include "undo_manager.h"

#include "../../ass_dialogue.h"
#include "../../ass_info.h"
#include "../../ass_style.h"

#include <cstdio>
#include <string>

namespace {

constexpr const char *kStylesFormatLine =
    "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, "
    "BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, "
    "BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding";
constexpr const char *kEventsFormatLine =
    "Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text";

std::string toUtf8StdString(const QString &s) {
    const QByteArray utf8 = s.toUtf8();
    return std::string(utf8.constData(), size_t(utf8.size()));
}

constexpr int kCoalesceWindowMs = 1000;

} // namespace

PropertiesViewModel::PropertiesViewModel(AssFile *file, TranslationService *i18n,
                                         UndoManager *undoManager, QObject *parent)
: QObject(parent)
, file_(file)
, i18n_(i18n)
, undo_manager_(undoManager)
, coalesce_timer_(this)
{
    coalesce_timer_.setSingleShot(true);
    coalesce_timer_.setInterval(kCoalesceWindowMs);
    connect(&coalesce_timer_, &QTimer::timeout, this, [this] { commit_id_ = -1; });

    if (undo_manager_)
        connect(undo_manager_, &UndoManager::restored, this, &PropertiesViewModel::onRestored);
}

void PropertiesViewModel::onRestored() {
    // Undo/redo may have replaced the whole [Script Info] section.
    commit_id_ = -1;
    emit scriptTitleChanged();
    emit playResXChanged();
    emit playResYChanged();
}

void PropertiesViewModel::commitScriptInfo(const QString &description) {
    if (!file_)
        return;
    commit_id_ =
        aegisub_qt::commit(file_, toUtf8StdString(description), AssFile::COMMIT_SCRIPTINFO, commit_id_);
    coalesce_timer_.start();
}

QString PropertiesViewModel::scriptTitle() const {
    return file_ ? QString::fromUtf8(file_->GetScriptInfo("Title").c_str()) : QString();
}

void PropertiesViewModel::setScriptTitle(const QString &title) {
    if (!file_ || scriptTitle() == title)
        return;
    file_->SetScriptInfo("Title", title.toUtf8().toStdString());
    commitScriptInfo(QStringLiteral("Edit script title"));
    emit scriptTitleChanged();
}

int PropertiesViewModel::playResX() const {
    return file_ ? file_->GetScriptInfoAsInt("PlayResX") : 0;
}

void PropertiesViewModel::setPlayResX(int value) {
    if (!file_ || playResX() == value)
        return;
    file_->SetScriptInfo("PlayResX", std::to_string(value));
    commitScriptInfo(QStringLiteral("Edit PlayResX"));
    emit playResXChanged();
}

int PropertiesViewModel::playResY() const {
    return file_ ? file_->GetScriptInfoAsInt("PlayResY") : 0;
}

void PropertiesViewModel::setPlayResY(int value) {
    if (!file_ || playResY() == value)
        return;
    file_->SetScriptInfo("PlayResY", std::to_string(value));
    commitScriptInfo(QStringLiteral("Edit PlayResY"));
    emit playResYChanged();
}

void PropertiesViewModel::dumpAss() {
    if (!file_) {
        std::printf("[dumpAss] no AssFile\n");
        std::fflush(stdout);
        return;
    }

    std::string out = "[Script Info]\n";
    for (auto const &info : file_->Info)
        out += info.GetEntryData() + "\n";

    out += "\n[V4+ Styles]\n";
    out += std::string(kStylesFormatLine) + "\n";
    for (auto const &style : file_->Styles)
        out += style.GetEntryData() + "\n";

    out += "\n[Events]\n";
    out += std::string(kEventsFormatLine) + "\n";
    for (auto const &line : file_->Events)
        out += line.GetEntryData() + "\n";

    std::printf("%s", out.c_str());
    std::fflush(stdout);
}
