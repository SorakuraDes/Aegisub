#include "ass_syntax_highlighter.h"

#include <QDebug>
#include <QGuiApplication>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <QQmlComponent>
#include <QQmlEngine>
#include <memory>

namespace {
QTextCharFormat styleAt(const QTextDocument &doc, int position) {
    const auto block = doc.findBlock(position);
    for (const auto &range : block.layout()->formats()) {
        const auto offset = position - block.position();
        if (offset >= range.start && offset < range.start + range.length)
            return range.format;
    }
    return {};
}
}

int main(int argc, char **argv) {
#ifndef Q_OS_WIN
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
    QGuiApplication app(argc, argv);
    QTextDocument doc;
    const QString original = QStringLiteral("\u4e2d\U0001f600{\\fad(300,300)\\1c&HFF0033&}\u6587\\N\\n\\h} {\\b1");
    doc.setPlainText(original);
    QTextCursor cursor(&doc);
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(QStringLiteral("!"));
    doc.setModified(false);
    const int undoSteps = doc.availableUndoSteps();
    AssSyntaxHighlighter hl;
    hl.setDocument(&doc);
    hl.rehighlight();

    auto at = [&](QString const &part) { return styleAt(doc, original.indexOf(part)); };
    auto fg = [&](QString const &part) { return at(part).foreground().color(); };
    auto check = [](bool ok, const char *what) {
        if (!ok) qCritical() << what;
        return ok;
    };
    bool ok = check(styleAt(doc, 0).isEmpty() && styleAt(doc, 2).isEmpty() && at(QStringLiteral("\u6587")).isEmpty(), "Unicode plain text must be untouched");
    ok &= check(fg("\\fad") == hl.primary() && fg("fad") == hl.primary(), "tag after UTF-16 surrogate pair");
    ok &= check(!at("300").isEmpty() && !at("&H").isEmpty() && fg("300") != fg("&H") && fg("300") != hl.primary(), "parameter vs color literal");
    ok &= check(!at("{").isEmpty() && fg("{") != hl.primary(), "override delimiter");
    ok &= check(!at("\\N").isEmpty() && fg("\\N") == fg("\\n") && fg("\\N") == fg("\\h"), "ASS escapes outside overrides");
    ok &= check(at("} {").underlineStyle() == QTextCharFormat::WaveUnderline && at("{\\b").underlineStyle() == QTextCharFormat::WaveUnderline, "extra closing brace and unclosed opening brace");
    const auto darkNumber = fg("300");
    hl.setDarkMode(false);
    hl.setPrimary(QColor(0x82, 0x27, 0x64));
    ok &= check(fg("fad") == hl.primary() && fg("300") != darkNumber, "live theme update");
    ok &= check(doc.toPlainText() == original + u'!' && !doc.isModified() && doc.availableUndoSteps() == undoSteps, "highlighting must not edit text or history");
    doc.undo();
    ok &= check(doc.toPlainText() == original, "first undo must remove user's edit");
    QQmlEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(import QtQuick
import QtQuick.Controls.Material
TextArea { property int count: 0; onTextChanged: ++count; textFormat: TextEdit.PlainText })", QUrl());
    std::unique_ptr<QObject> editor(component.create());
    if (!editor) { qCritical() << component.errors(); return 1; }
    editor->setProperty("text", original);
    auto target = editor->property("textDocument").value<QQuickTextDocument *>();
    AssSyntaxHighlighter attached;
    attached.setTarget(target);
    QCoreApplication::processEvents();
    const int before = editor->property("count").toInt();
    attached.setDarkMode(false);
    attached.setPrimary(Qt::darkGreen);
    QCoreApplication::processEvents();
    ok &= check(target && editor->property("count").toInt() == before, "theme refresh must not signal TextArea.textChanged");
    return ok ? 0 : 1;
}
