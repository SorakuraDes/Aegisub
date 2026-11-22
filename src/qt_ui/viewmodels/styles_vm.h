#pragma once

// agi::signal (via ass_file.h) has members named `slots`; keep this before any
// Qt header for the same reason ass_file.h is included first in the .cpp.
#include <libaegisub/signal.h>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <string>
#include <vector>

class AssDialogue;
class AssFile;
class AssStyle;
class UndoManager;

/// View model for the styles manager: edits the file's [V4+ Styles] entries.
///
/// Every modification goes through AssFile::Commit(COMMIT_STYLES) so it lands on
/// the shared undo stack; a style rename commits COMMIT_STYLES | COMMIT_DIAG_META
/// after also updating the dialogue lines that referenced the old name.
class StylesViewModel final : public QObject {
    Q_OBJECT

    Q_PROPERTY(QStringList styleNames READ styleNames NOTIFY styleNamesChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)
    /// Human readable feedback for failed operations (e.g. duplicate name).
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

    Q_PROPERTY(QString name READ name WRITE setName NOTIFY styleChanged)
    Q_PROPERTY(QString fontName READ fontName WRITE setFontName NOTIFY styleChanged)
    Q_PROPERTY(double fontSize READ fontSize WRITE setFontSize NOTIFY styleChanged)
    Q_PROPERTY(bool bold READ bold WRITE setBold NOTIFY styleChanged)
    Q_PROPERTY(bool italic READ italic WRITE setItalic NOTIFY styleChanged)
    Q_PROPERTY(bool underline READ underline WRITE setUnderline NOTIFY styleChanged)
    Q_PROPERTY(bool strikeout READ strikeout WRITE setStrikeout NOTIFY styleChanged)

    /// "#RRGGBB" (the alpha is exposed separately as an opacity 0-255).
    Q_PROPERTY(QString primaryColor READ primaryColor WRITE setPrimaryColor NOTIFY styleChanged)
    Q_PROPERTY(QString secondaryColor READ secondaryColor WRITE setSecondaryColor NOTIFY styleChanged)
    Q_PROPERTY(QString outlineColor READ outlineColor WRITE setOutlineColor NOTIFY styleChanged)
    Q_PROPERTY(QString shadowColor READ shadowColor WRITE setShadowColor NOTIFY styleChanged)
    /// Opacity 0-255 (255 = fully opaque); ASS stores the inverted value.
    Q_PROPERTY(int primaryAlpha READ primaryAlpha WRITE setPrimaryAlpha NOTIFY styleChanged)
    Q_PROPERTY(int secondaryAlpha READ secondaryAlpha WRITE setSecondaryAlpha NOTIFY styleChanged)
    Q_PROPERTY(int outlineAlpha READ outlineAlpha WRITE setOutlineAlpha NOTIFY styleChanged)
    Q_PROPERTY(int shadowAlpha READ shadowAlpha WRITE setShadowAlpha NOTIFY styleChanged)

    /// \an-style alignment, 1-9 (numpad layout).
    Q_PROPERTY(int alignment READ alignment WRITE setAlignment NOTIFY styleChanged)
    Q_PROPERTY(double outlineWidth READ outlineWidth WRITE setOutlineWidth NOTIFY styleChanged)
    Q_PROPERTY(double shadowDepth READ shadowDepth WRITE setShadowDepth NOTIFY styleChanged)
    /// 1 = outline + drop shadow, 3 = opaque box.
    Q_PROPERTY(int borderStyle READ borderStyle WRITE setBorderStyle NOTIFY styleChanged)
    Q_PROPERTY(int marginL READ marginL WRITE setMarginL NOTIFY styleChanged)
    Q_PROPERTY(int marginR READ marginR WRITE setMarginR NOTIFY styleChanged)
    Q_PROPERTY(int marginV READ marginV WRITE setMarginV NOTIFY styleChanged)

public:
    StylesViewModel(AssFile *file, UndoManager *undoManager, QObject *parent = nullptr);

    QStringList styleNames() const;
    int currentIndex() const;
    void setCurrentIndex(int index);
    QString lastError() const;

    QString name() const;
    void setName(const QString &value);
    QString fontName() const;
    void setFontName(const QString &value);
    double fontSize() const;
    void setFontSize(double value);
    bool bold() const;
    void setBold(bool value);
    bool italic() const;
    void setItalic(bool value);
    bool underline() const;
    void setUnderline(bool value);
    bool strikeout() const;
    void setStrikeout(bool value);

    QString primaryColor() const;
    void setPrimaryColor(const QString &value);
    QString secondaryColor() const;
    void setSecondaryColor(const QString &value);
    QString outlineColor() const;
    void setOutlineColor(const QString &value);
    QString shadowColor() const;
    void setShadowColor(const QString &value);
    int primaryAlpha() const;
    void setPrimaryAlpha(int value);
    int secondaryAlpha() const;
    void setSecondaryAlpha(int value);
    int outlineAlpha() const;
    void setOutlineAlpha(int value);
    int shadowAlpha() const;
    void setShadowAlpha(int value);

    int alignment() const;
    void setAlignment(int value);
    double outlineWidth() const;
    void setOutlineWidth(double value);
    double shadowDepth() const;
    void setShadowDepth(double value);
    int borderStyle() const;
    void setBorderStyle(int value);
    int marginL() const;
    void setMarginL(int value);
    int marginR() const;
    void setMarginR(int value);
    int marginV() const;
    void setMarginV(int value);

    /// Add a new style with a unique name; returns its index.
    Q_INVOKABLE int newStyle();    /// Deep-copy style `index` under a unique name; returns the copy's index.
    Q_INVOKABLE int copyStyle(int index);
    /// Delete style `index` (the file always keeps at least one style).
    Q_INVOKABLE bool deleteStyle(int index);
    Q_INVOKABLE void clearError();

    /// A new document was created/opened: rebuild the style index and form.
    void handleDocumentReload();

signals:
    void styleNamesChanged();
    void currentIndexChanged();
    void styleChanged();
    void lastErrorChanged();
    /// The selected style changed from the outside (selection, undo, new file):
    /// the form should refill its fields, overwriting even focused ones.
    void reloadForm();

private:
    AssStyle *current() const;
    AssStyle *styleAt(int index) const;
    /// Rebuild the style index from the file. `force_reload` also tells QML to
    /// refill the form even when the structure did not change (undo).
    void refreshStyles(bool force_reload);
    /// Serialize the style (AssStyle caches its ASS line) and commit it.
    void finishChange(const QString &description);
    void commitStyles(const QString &description, int extra_type = 0);
    void resetCoalescing();
    bool nameTaken(const QString &name, const AssStyle *except) const;
    QString uniqueStyleName(const QString &base) const;
    void renameLineReferences(const std::string &old_name, const std::string &new_name);
    void setError(const QString &message);
    void onFileCommitted(int type, const AssDialogue *single_line);
    void onRestored();

    AssFile *file_;
    UndoManager *undo_manager_;
    std::vector<AssStyle *> styles_;
    QStringList style_names_;
    int current_index_ = 0;
    QString last_error_;
    /// Coalesces rapid edits of the same style into one undo step.
    QTimer coalesce_timer_;
    int commit_id_ = -1;
    agi::signal::Connection commit_connection_;
};
