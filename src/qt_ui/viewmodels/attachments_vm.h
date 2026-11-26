#pragma once

#include <libaegisub/signal.h>

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QUrl>
#include <QVariantList>

#include <vector>

class AssDialogue;
class AssFile;
class DocumentViewModel;
class UndoManager;

class AttachmentsViewModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    enum Role { NameRole = Qt::UserRole + 1, GroupRole, SizeRole };

    AttachmentsViewModel(AssFile *file, UndoManager *undoManager,
                         DocumentViewModel *documentVM, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return rowCount(); }
    QString lastError() const { return last_error_; }

    Q_INVOKABLE bool attachFonts(const QList<QUrl> &files);
    Q_INVOKABLE bool attachGraphics(const QList<QUrl> &files);
    Q_INVOKABLE bool deleteAttachments(const QVariantList &rows);
    Q_INVOKABLE QUrl extractionFileUrl(int row);
    Q_INVOKABLE bool validateExtraction(const QVariantList &rows);
    Q_INVOKABLE bool extractAttachment(int row, const QUrl &file);
    Q_INVOKABLE bool extractAttachments(const QVariantList &rows, const QUrl &folder);
    Q_INVOKABLE void clearError();

signals:
    void countChanged();
    void lastErrorChanged();

private:
    struct Row {
        QString name;
        QString group;
        qulonglong size;
    };

    void refresh();
    void onFileCommitted(int type, const AssDialogue *);
    bool fail(const QString &message);
    bool attachFiles(const QList<QUrl> &files, bool fonts);
    bool getSelectedRows(const QVariantList &input, std::vector<int> &rows);
    bool extractionName(int row, QString &name);
    bool writeAttachment(int row, const QString &path);

    AssFile *file_;
    QString document_path_;
    QString last_error_;
    std::vector<Row> rows_;
    agi::signal::Connection commit_connection_;
};
