#include "ass_syntax_highlighter.h"

#include <QRegularExpression>
#include <QSignalBlocker>
#include <QTextCharFormat>
#include <libaegisub/ass/dialogue_parser.h>

AssSyntaxHighlighter::AssSyntaxHighlighter(QObject *parent)
: QSyntaxHighlighter(parent) {}

void AssSyntaxHighlighter::setTarget(QQuickTextDocument *target) {
    if (target_ == target) return;
    target_ = target;
    setDocument(target ? target->textDocument() : nullptr);
    refreshFormats(); // Flush Qt's deferred first pass before returning to QML.
    emit targetChanged();
}

void AssSyntaxHighlighter::setPrimary(const QColor &color) {
    if (!color.isValid() || color == primary_) return;
    primary_ = color;
    refreshFormats();
    emit primaryChanged();
}

void AssSyntaxHighlighter::setDarkMode(bool dark) {
    if (dark == dark_) return;
    dark_ = dark;
    refreshFormats();
    emit darkModeChanged();
}

void AssSyntaxHighlighter::setColors(const QVariantMap &colors) {
    if (colors_ == colors) return;
    colors_ = colors;
    refreshFormats();
    emit colorsChanged();
}

void AssSyntaxHighlighter::refreshFormats() {
    // Qt Quick 6.6 forwards QTextDocument's formatting notification as a text
    // edit. The document/layout still needs those notifications to repaint.
    // Silence only the owning TextEdit's outward signals during this pass.
    const QSignalBlocker guard(target_ ? target_->parent() : nullptr);
    rehighlight();
}

void AssSyntaxHighlighter::highlightBlock(const QString &text) {
    namespace dt = agi::ass::DialogueTokenType;
    static const QRegularExpression assHex(QStringLiteral("&[Hh][0-9A-Fa-f]{1,8}&?"));
    const QColor punctuation = dark_ ? QColor(0xc2,0xca,0xcd) : QColor(0x47,0x55,0x5a);
    const auto effective = [this](const char *key, const QColor &fallback) {
        const auto color = colors_.value(QLatin1String(key)).value<QColor>();
        return color.isValid() ? color : fallback;
    };
    const QColor brackets = effective("Brackets", punctuation);
    const QColor tags = effective("Tags", primary_);
    const QColor parameter = effective("Parameters", dark_ ? QColor(0xff,0xc6,0x85) : QColor(0x83,0x46,0x00));
    const QColor hex = effective("Parameters", dark_ ? QColor(0xaf,0xdf,0xa5) : QColor(0x26,0x68,0x38));
    const QColor escape = effective("Line Break", dark_ ? QColor(0xd7,0xb9,0xff) : QColor(0x6d,0x42,0x95));
    QTextCharFormat error;
    error.setForeground(effective("Error", dark_ ? QColor(0xff,0xb4,0xab) : QColor(0xba,0x1a,0x1a)));
    error.setUnderlineStyle(QTextCharFormat::WaveUnderline);

    const auto bytes = text.toStdString(); // UTF-8; Qt layout offsets are UTF-16.
    size_t byte = 0;
    int end = 0, open = -1;
    for (const auto &token : agi::ass::TokenizeDialogueBody(bytes)) {
        const int start = end;
        const size_t next = byte + token.length;
        for (; byte < next; ++byte) {
            const unsigned char c = static_cast<unsigned char>(bytes[byte]);
            if ((c & 0xc0) != 0x80) end += c >= 0xf0 ? 2 : 1;
        }
        const int length = end - start;
        switch (token.type) {
        case dt::OVR_BEGIN:
            open = start;
            setFormat(start, length, brackets);
            break;
        case dt::OVR_END:
            open = -1;
            setFormat(start, length, brackets);
            break;
        case dt::ERROR:
            setFormat(start, length, error);
            break;
        case dt::TEXT:
            // The renderer treats stray closing braces as text. Flag only the brace.
            for (int i = start; i < end; ++i)
                if (text.at(i) == u'}') setFormat(i, 1, error);
            break;
        case dt::TAG_START:
            setFormat(start, length, primary_);
            break;
        case dt::TAG_NAME:
            setFormat(start, length, tags);
            break;
        case dt::OPEN_PAREN: case dt::CLOSE_PAREN: case dt::ARG_SEP:
            setFormat(start, length, punctuation);
            break;
        case dt::ARG: {
            setFormat(start, length, parameter);
            auto matches = assHex.globalMatch(text.mid(start, length));
            while (matches.hasNext()) {
                const auto m = matches.next();
                setFormat(start + m.capturedStart(), m.capturedLength(), hex);
            }
            break;
        }
        case dt::LINE_BREAK:
            setFormat(start, length, escape);
            break;
        }
    }
    if (open >= 0) setFormat(open, 1, error);
}
