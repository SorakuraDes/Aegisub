// Must come before any Qt header: agi::signal (via ass_file.h) has members
// named `slots`, and Qt defines `signals`/`slots` as macros.
#include "../../ass_file.h"

#include "styles_vm.h"
#include "undo_manager.h"

#include "../commit_bridge.h"

#include "../../ass_dialogue.h"
// ass_override.h is already pulled in by ass_dialogue.h and has no include
// guard, so it must not be included again here.
#include "../../ass_style.h"

#include <libaegisub/color.h>

#include <QColor>

#include <algorithm>
#include <string>

namespace {

QString toQString(std::string const &s) {
    return QString::fromUtf8(s.data(), int(s.size()));
}

std::string toStdString(QString const &s) {
    const QByteArray utf8 = s.toUtf8();
    return std::string(utf8.constData(), size_t(utf8.size()));
}

QString colorToHex(const agi::Color &color) {
    return QStringLiteral("#%1%2%3")
        .arg(int(color.r), 2, 16, QChar('0'))
        .arg(int(color.g), 2, 16, QChar('0'))
        .arg(int(color.b), 2, 16, QChar('0'));
}

/// ASS stores alpha inverted (0 = opaque); the UI exposes an opacity instead.
int colorToOpacity(const agi::Color &color) {
    return 255 - int(color.a);
}

void setColorHex(agi::Color &color, const QString &hex) {
    const QColor parsed(hex.trimmed());
    if (!parsed.isValid())
        return;
    color.r = (unsigned char)parsed.red();
    color.g = (unsigned char)parsed.green();
    color.b = (unsigned char)parsed.blue();
}

void setColorOpacity(agi::Color &color, int opacity) {
    color.a = (unsigned char)(255 - std::clamp(opacity, 0, 255));
}

struct TagRenameData {
    const std::string *old_name = nullptr;
    const std::string *new_name = nullptr;
    bool changed = false;
};

/// Callback for AssDialogueBlockOverride::ProcessParameters: rewrites \r<name>.
void renameTag(std::string const &tag, AssOverrideParameter *param, void *user_data) {
    auto *data = static_cast<TagRenameData *>(user_data);
    if (tag != "\\r" || param->GetType() != VariableDataType::TEXT)
        return;
    if (param->Get<std::string>() != *data->old_name)
        return;
    param->Set(*data->new_name);
    data->changed = true;
}

constexpr int kCoalesceWindowMs = 1000;

} // namespace

StylesViewModel::StylesViewModel(AssFile *file, UndoManager *undoManager, QObject *parent)
: QObject(parent)
, file_(file)
, undo_manager_(undoManager)
, coalesce_timer_(this)
{
    coalesce_timer_.setSingleShot(true);
    coalesce_timer_.setInterval(kCoalesceWindowMs);
    connect(&coalesce_timer_, &QTimer::timeout, this, [this] { commit_id_ = -1; });

    if (file_)
        commit_connection_ = file_->AddCommitListener(&StylesViewModel::onFileCommitted, this);
    if (undo_manager_)
        connect(undo_manager_, &UndoManager::restored, this, &StylesViewModel::onRestored);

    refreshStyles(true);
}

AssStyle *StylesViewModel::current() const {
    return styleAt(current_index_);
}

AssStyle *StylesViewModel::styleAt(int index) const {
    if (index < 0 || index >= int(styles_.size()))
        return nullptr;
    return styles_[size_t(index)];
}

void StylesViewModel::refreshStyles(bool force_reload) {
    // The name is cached as a string: after a file swap the old AssStyle
    // pointers are dangling, so they must not be dereferenced here.
    const QString previous_name = style_names_.value(current_index_);
    const int previous_index = current_index_;

    styles_.clear();
    QStringList names;
    if (file_) {
        styles_.reserve(file_->Styles.size());
        for (auto &style : file_->Styles) {
            styles_.push_back(&style);
            names << QString::fromStdString(style.name);
        }
    }

    const bool names_changed = names != style_names_;
    style_names_ = names;
    if (names_changed)
        emit styleNamesChanged();

    int index = names.indexOf(previous_name);
    if (index < 0)
        index = names.isEmpty() ? -1 : std::clamp(previous_index, 0, int(names.size()) - 1);
    if (style_names_.isEmpty())
        index = -1;

    const bool index_changed = index != current_index_;
    current_index_ = index;
    if (index_changed)
        emit currentIndexChanged();

    if (names_changed || index_changed || force_reload)
        emit reloadForm();
}

void StylesViewModel::onFileCommitted(int type, const AssDialogue *) {
    if (type == AssFile::COMMIT_NEW || (type & AssFile::COMMIT_STYLES))
        refreshStyles(false);
}

void StylesViewModel::onRestored() {
    // The snapshot was swapped in: every AssStyle pointer is new and every value
    // may have changed, so the form has to be refilled even if nothing moved.
    refreshStyles(true);
}

void StylesViewModel::handleDocumentReload() {
    // New/Open swaps the AssFile without going through Commit, so the commit
    // listener never fires for it.
    refreshStyles(true);
}

QStringList StylesViewModel::styleNames() const {
    return style_names_;
}

int StylesViewModel::currentIndex() const {
    return current_index_;
}

void StylesViewModel::setCurrentIndex(int index) {
    if (style_names_.isEmpty())
        index = -1;
    else
        index = std::clamp(index, 0, int(style_names_.size()) - 1);
    if (index == current_index_)
        return;
    current_index_ = index;
    resetCoalescing();
    emit currentIndexChanged();
    emit reloadForm();
}

QString StylesViewModel::lastError() const {
    return last_error_;
}

void StylesViewModel::setError(const QString &message) {
    if (last_error_ == message)
        return;
    last_error_ = message;
    emit lastErrorChanged();
}

void StylesViewModel::clearError() {
    setError(QString());
}

void StylesViewModel::resetCoalescing() {
    commit_id_ = -1;
    coalesce_timer_.stop();
}

void StylesViewModel::commitStyles(const QString &description, int extra_type) {
    if (!file_)
        return;
    aegisub_qt::commit(file_, toStdString(description), AssFile::COMMIT_STYLES | extra_type, -1,
                       nullptr);
}

void StylesViewModel::finishChange(const QString &description) {
    if (!file_)
        return;
    if (AssStyle *style = current())
        style->UpdateData();
    // Coalesce rapid edits (typing a size, dragging a spin box) into one step.
    commit_id_ =
        aegisub_qt::commit(file_, toStdString(description), AssFile::COMMIT_STYLES, commit_id_,
                          nullptr);
    coalesce_timer_.start();
    emit styleChanged();
}

bool StylesViewModel::nameTaken(const QString &name, const AssStyle *except) const {
    for (const AssStyle *style : styles_) {
        if (style == except)
            continue;
        if (QString::fromStdString(style->name).compare(name, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

QString StylesViewModel::uniqueStyleName(const QString &base) const {
    if (!nameTaken(base, nullptr))
        return base;
    for (int i = 2;; ++i) {
        const QString candidate = QStringLiteral("%1 %2").arg(base).arg(i);
        if (!nameTaken(candidate, nullptr))
            return candidate;
    }
}

void StylesViewModel::renameLineReferences(const std::string &old_name,
                                           const std::string &new_name) {
    if (!file_)
        return;

    TagRenameData tag_data{&old_name, &new_name, false};
    for (auto &line : file_->Events) {
        if (line.Style.get() == old_name)
            line.Style = new_name;

        // Also rewrite override tags that force this style: {\rOldName}.
        tag_data.changed = false;
        auto blocks = line.ParseTags();
        for (auto &block : blocks) {
            auto *override_block = dynamic_cast<AssDialogueBlockOverride *>(block.get());
            if (!override_block)
                continue;
            override_block->ProcessParameters(&renameTag, &tag_data);
        }
        if (tag_data.changed)
            line.UpdateText(blocks);
    }
}

// ---------------------------------------------------------------------------
// Style identity and lifetime
// ---------------------------------------------------------------------------

QString StylesViewModel::name() const {
    const AssStyle *style = current();
    return style ? QString::fromStdString(style->name) : QString();
}

void StylesViewModel::setName(const QString &value) {
    AssStyle *style = current();
    if (!style)
        return;

    const QString new_name = value.trimmed();
    const QString old_name = QString::fromStdString(style->name);
    if (new_name == old_name) {
        clearError();
        return;
    }
    if (new_name.isEmpty()) {
        setError(QStringLiteral("Style name cannot be empty"));
        emit reloadForm();
        return;
    }
    if (nameTaken(new_name, style)) {
        setError(QStringLiteral("A style with this name already exists"));
        emit reloadForm();
        return;
    }

    const std::string old_std = style->name;
    const std::string new_std = toStdString(new_name);

    style->name = new_std;
    style->UpdateData();
    renameLineReferences(old_std, new_std);

    resetCoalescing();
    commitStyles(QStringLiteral("Rename style"), AssFile::COMMIT_DIAG_META);
    clearError();
    emit styleChanged();
}

int StylesViewModel::newStyle() {
    if (!file_)
        return -1;

    auto *style = new AssStyle;
    style->name = toStdString(uniqueStyleName(QStringLiteral("New Style")));
    style->UpdateData();
    file_->Styles.push_back(*style);

    resetCoalescing();
    commitStyles(QStringLiteral("New style"));

    // refreshStyles() ran during the commit; find the new style by pointer.
    for (int i = 0; i < int(styles_.size()); ++i) {
        if (styles_[size_t(i)] == style) {
            setCurrentIndex(i);
            clearError();
            return i;
        }
    }
    return -1;
}

int StylesViewModel::copyStyle(int index) {
    if (!file_)
        return -1;
    AssStyle *source = styleAt(index);
    if (!source)
        return -1;

    auto *copy = new AssStyle(*source);
    copy->name = toStdString(
        uniqueStyleName(QStringLiteral("%1 copy").arg(QString::fromStdString(source->name))));
    copy->UpdateData();

    auto position = file_->Styles.iterator_to(*source);
    file_->Styles.insert(++position, *copy);

    resetCoalescing();
    commitStyles(QStringLiteral("Copy style"));

    for (int i = 0; i < int(styles_.size()); ++i) {
        if (styles_[size_t(i)] == copy) {
            setCurrentIndex(i);
            clearError();
            return i;
        }
    }
    return -1;
}

bool StylesViewModel::deleteStyle(int index) {
    if (!file_)
        return false;
    // The script must keep at least one style.
    if (int(styles_.size()) <= 1) {
        setError(QStringLiteral("Cannot delete the last style"));
        return false;
    }

    AssStyle *style = styleAt(index);
    if (!style)
        return false;

    file_->Styles.erase(file_->Styles.iterator_to(*style));
    delete style;

    resetCoalescing();
    commitStyles(QStringLiteral("Delete style"));
    clearError();
    return true;
}

// ---------------------------------------------------------------------------
// Typography
// ---------------------------------------------------------------------------

QString StylesViewModel::fontName() const {
    const AssStyle *style = current();
    return style ? QString::fromStdString(style->font) : QString();
}

void StylesViewModel::setFontName(const QString &value) {
    AssStyle *style = current();
    if (!style)
        return;
    const std::string font = toStdString(value);
    if (style->font == font)
        return;
    style->font = font;
    finishChange(QStringLiteral("Edit style font"));
}

double StylesViewModel::fontSize() const {
    const AssStyle *style = current();
    return style ? style->fontsize : 0.;
}

void StylesViewModel::setFontSize(double value) {
    AssStyle *style = current();
    if (!style || style->fontsize == value)
        return;
    style->fontsize = value;
    finishChange(QStringLiteral("Edit style font size"));
}

bool StylesViewModel::bold() const {
    const AssStyle *style = current();
    return style ? style->bold : false;
}

void StylesViewModel::setBold(bool value) {
    AssStyle *style = current();
    if (!style || style->bold == value)
        return;
    style->bold = value;
    finishChange(QStringLiteral("Toggle bold"));
}

bool StylesViewModel::italic() const {
    const AssStyle *style = current();
    return style ? style->italic : false;
}

void StylesViewModel::setItalic(bool value) {
    AssStyle *style = current();
    if (!style || style->italic == value)
        return;
    style->italic = value;
    finishChange(QStringLiteral("Toggle italic"));
}

bool StylesViewModel::underline() const {
    const AssStyle *style = current();
    return style ? style->underline : false;
}

void StylesViewModel::setUnderline(bool value) {
    AssStyle *style = current();
    if (!style || style->underline == value)
        return;
    style->underline = value;
    finishChange(QStringLiteral("Toggle underline"));
}

bool StylesViewModel::strikeout() const {
    const AssStyle *style = current();
    return style ? style->strikeout : false;
}

void StylesViewModel::setStrikeout(bool value) {
    AssStyle *style = current();
    if (!style || style->strikeout == value)
        return;
    style->strikeout = value;
    finishChange(QStringLiteral("Toggle strikeout"));
}

// ---------------------------------------------------------------------------
// Colors
// ---------------------------------------------------------------------------

QString StylesViewModel::primaryColor() const {
    const AssStyle *style = current();
    return style ? colorToHex(style->primary) : QString();
}

void StylesViewModel::setPrimaryColor(const QString &value) {
    AssStyle *style = current();
    if (!style)
        return;
    agi::Color color = style->primary;
    setColorHex(color, value);
    if (color == style->primary)
        return;
    style->primary = color;
    finishChange(QStringLiteral("Edit primary color"));
}

QString StylesViewModel::secondaryColor() const {
    const AssStyle *style = current();
    return style ? colorToHex(style->secondary) : QString();
}

void StylesViewModel::setSecondaryColor(const QString &value) {
    AssStyle *style = current();
    if (!style)
        return;
    agi::Color color = style->secondary;
    setColorHex(color, value);
    if (color == style->secondary)
        return;
    style->secondary = color;
    finishChange(QStringLiteral("Edit secondary color"));
}

QString StylesViewModel::outlineColor() const {
    const AssStyle *style = current();
    return style ? colorToHex(style->outline) : QString();
}

void StylesViewModel::setOutlineColor(const QString &value) {
    AssStyle *style = current();
    if (!style)
        return;
    agi::Color color = style->outline;
    setColorHex(color, value);
    if (color == style->outline)
        return;
    style->outline = color;
    finishChange(QStringLiteral("Edit outline color"));
}

QString StylesViewModel::shadowColor() const {
    const AssStyle *style = current();
    return style ? colorToHex(style->shadow) : QString();
}

void StylesViewModel::setShadowColor(const QString &value) {
    AssStyle *style = current();
    if (!style)
        return;
    agi::Color color = style->shadow;
    setColorHex(color, value);
    if (color == style->shadow)
        return;
    style->shadow = color;
    finishChange(QStringLiteral("Edit shadow color"));
}

int StylesViewModel::primaryAlpha() const {
    const AssStyle *style = current();
    return style ? colorToOpacity(style->primary) : 255;
}

void StylesViewModel::setPrimaryAlpha(int value) {
    AssStyle *style = current();
    if (!style)
        return;
    agi::Color color = style->primary;
    setColorOpacity(color, value);
    if (color == style->primary)
        return;
    style->primary = color;
    finishChange(QStringLiteral("Edit primary alpha"));
}

int StylesViewModel::secondaryAlpha() const {
    const AssStyle *style = current();
    return style ? colorToOpacity(style->secondary) : 255;
}

void StylesViewModel::setSecondaryAlpha(int value) {
    AssStyle *style = current();
    if (!style)
        return;
    agi::Color color = style->secondary;
    setColorOpacity(color, value);
    if (color == style->secondary)
        return;
    style->secondary = color;
    finishChange(QStringLiteral("Edit secondary alpha"));
}

int StylesViewModel::outlineAlpha() const {
    const AssStyle *style = current();
    return style ? colorToOpacity(style->outline) : 255;
}

void StylesViewModel::setOutlineAlpha(int value) {
    AssStyle *style = current();
    if (!style)
        return;
    agi::Color color = style->outline;
    setColorOpacity(color, value);
    if (color == style->outline)
        return;
    style->outline = color;
    finishChange(QStringLiteral("Edit outline alpha"));
}

int StylesViewModel::shadowAlpha() const {
    const AssStyle *style = current();
    return style ? colorToOpacity(style->shadow) : 255;
}

void StylesViewModel::setShadowAlpha(int value) {
    AssStyle *style = current();
    if (!style)
        return;
    agi::Color color = style->shadow;
    setColorOpacity(color, value);
    if (color == style->shadow)
        return;
    style->shadow = color;
    finishChange(QStringLiteral("Edit shadow alpha"));
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

int StylesViewModel::alignment() const {
    const AssStyle *style = current();
    return style ? style->alignment : 2;
}

void StylesViewModel::setAlignment(int value) {
    AssStyle *style = current();
    if (!style)
        return;
    const int clamped = std::clamp(value, 1, 9);
    if (style->alignment == clamped)
        return;
    style->alignment = clamped;
    finishChange(QStringLiteral("Edit style alignment"));
}

double StylesViewModel::outlineWidth() const {
    const AssStyle *style = current();
    return style ? style->outline_w : 0.;
}

void StylesViewModel::setOutlineWidth(double value) {
    AssStyle *style = current();
    if (!style || style->outline_w == value)
        return;
    style->outline_w = value;
    finishChange(QStringLiteral("Edit outline width"));
}

double StylesViewModel::shadowDepth() const {
    const AssStyle *style = current();
    return style ? style->shadow_w : 0.;
}

void StylesViewModel::setShadowDepth(double value) {
    AssStyle *style = current();
    if (!style || style->shadow_w == value)
        return;
    style->shadow_w = value;
    finishChange(QStringLiteral("Edit shadow depth"));
}

int StylesViewModel::borderStyle() const {
    const AssStyle *style = current();
    return style ? style->borderstyle : 1;
}

void StylesViewModel::setBorderStyle(int value) {
    AssStyle *style = current();
    if (!style)
        return;
    const int clamped = value == 3 ? 3 : 1;
    if (style->borderstyle == clamped)
        return;
    style->borderstyle = clamped;
    finishChange(QStringLiteral("Edit border style"));
}

int StylesViewModel::marginL() const {
    const AssStyle *style = current();
    return style ? style->Margin[0] : 0;
}

void StylesViewModel::setMarginL(int value) {
    AssStyle *style = current();
    if (!style)
        return;
    const int clamped = std::max(0, value);
    if (style->Margin[0] == clamped)
        return;
    style->Margin[0] = clamped;
    finishChange(QStringLiteral("Edit left margin"));
}

int StylesViewModel::marginR() const {
    const AssStyle *style = current();
    return style ? style->Margin[1] : 0;
}

void StylesViewModel::setMarginR(int value) {
    AssStyle *style = current();
    if (!style)
        return;
    const int clamped = std::max(0, value);
    if (style->Margin[1] == clamped)
        return;
    style->Margin[1] = clamped;
    finishChange(QStringLiteral("Edit right margin"));
}

int StylesViewModel::marginV() const {
    const AssStyle *style = current();
    return style ? style->Margin[2] : 0;
}

void StylesViewModel::setMarginV(int value) {
    AssStyle *style = current();
    if (!style)
        return;
    const int clamped = std::max(0, value);
    if (style->Margin[2] == clamped)
        return;
    style->Margin[2] = clamped;
    finishChange(QStringLiteral("Edit vertical margin"));
}
