#pragma once

// Parse agi::signal before Qt defines the slots macro.
#include <libaegisub/signal.h>

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantList>

#include <vector>

class AssFile;
class SubtitleGridModel;

class PasteOverViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList fields READ fields NOTIFY fieldsChanged)
    Q_PROPERTY(bool canAccept READ canAccept NOTIFY canAcceptChanged)

public:
    PasteOverViewModel(AssFile *file, SubtitleGridModel *grid, QObject *parent = nullptr);

    QVariantList fields() const;
    bool canAccept() const;

    Q_INVOKABLE bool prepare();
    Q_INVOKABLE void setField(int index, bool checked);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void selectNone();
    Q_INVOKABLE void selectTimes();
    Q_INVOKABLE void selectText();
    Q_INVOKABLE bool accept();
    Q_INVOKABLE void cancel();

signals:
    void fieldsChanged();
    void canAcceptChanged();

private:
    void setFields(std::vector<bool> fields);

    AssFile *file_;
    QPointer<SubtitleGridModel> grid_;
    QStringList entries_;
    std::vector<bool> fields_;
    agi::signal::Connection commit_connection_;
};
