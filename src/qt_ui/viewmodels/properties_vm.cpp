// Must come before any Qt header: agi::signal (via ass_file.h) has members
// named `slots`, and Qt defines `signals`/`slots` as macros.
#include "../../ass_file.h"

#include "properties_vm.h"

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
}

PropertiesViewModel::PropertiesViewModel(AssFile *file, TranslationService *i18n, QObject *parent)
: QObject(parent)
, file_(file)
, i18n_(i18n)
{
}

QString PropertiesViewModel::scriptTitle() const {
    return file_ ? QString::fromUtf8(file_->GetScriptInfo("Title").c_str()) : QString();
}

void PropertiesViewModel::setScriptTitle(const QString &title) {
    if (!file_ || scriptTitle() == title)
        return;
    file_->SetScriptInfo("Title", title.toUtf8().toStdString());
    emit scriptTitleChanged();
    emit edited();
}

int PropertiesViewModel::playResX() const {
    return file_ ? file_->GetScriptInfoAsInt("PlayResX") : 0;
}

void PropertiesViewModel::setPlayResX(int value) {
    if (!file_ || playResX() == value)
        return;
    file_->SetScriptInfo("PlayResX", std::to_string(value));
    emit playResXChanged();
    emit edited();
}

int PropertiesViewModel::playResY() const {
    return file_ ? file_->GetScriptInfoAsInt("PlayResY") : 0;
}

void PropertiesViewModel::setPlayResY(int value) {
    if (!file_ || playResY() == value)
        return;
    file_->SetScriptInfo("PlayResY", std::to_string(value));
    emit playResYChanged();
    emit edited();
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
