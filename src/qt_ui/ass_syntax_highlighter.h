#pragma once

#include <QColor>
#include <QPointer>
#include <QQuickTextDocument>
#include <QSyntaxHighlighter>
#include <QVariantMap>

class AssSyntaxHighlighter : public QSyntaxHighlighter {
    Q_OBJECT
    Q_PROPERTY(QQuickTextDocument *target READ target WRITE setTarget NOTIFY targetChanged)
    Q_PROPERTY(QColor primary READ primary WRITE setPrimary NOTIFY primaryChanged)
    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY darkModeChanged)
    Q_PROPERTY(QVariantMap colors READ colors WRITE setColors NOTIFY colorsChanged)

public:
    explicit AssSyntaxHighlighter(QObject *parent = nullptr);
    QQuickTextDocument *target() const { return target_; }
    void setTarget(QQuickTextDocument *target);
    QColor primary() const { return primary_; }
    void setPrimary(const QColor &color);
    bool darkMode() const { return dark_; }
    void setDarkMode(bool dark);
    QVariantMap colors() const { return colors_; }
    void setColors(const QVariantMap &colors);

signals:
    void targetChanged();
    void primaryChanged();
    void darkModeChanged();
    void colorsChanged();

protected:
    void highlightBlock(const QString &text) override;

private:
    void refreshFormats();
    QPointer<QQuickTextDocument> target_;
    QVariantMap colors_;
    QColor primary_{0x80, 0xde, 0xea};
    bool dark_ = true;
};
