#pragma once

#include <QObject>
#include <QList>
#include <QUrl>
#include <QVariantList>
#include <memory>

class AssFile;
class AudioViewModel;
class DocumentViewModel;
class SubtitleGridModel;
class TranslationService;
class VideoViewModel;

namespace aegisub_qt::automation {

class AutomationViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList scripts READ scripts NOTIFY scriptsChanged)
    Q_PROPERTY(QVariantList menu READ menu NOTIFY menuChanged)
    Q_PROPERTY(QUrl lastFolder READ lastFolder NOTIFY scriptsChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY operationChanged)
    Q_PROPERTY(bool progressVisible READ progressVisible NOTIFY operationChanged)
    Q_PROPERTY(bool cancelling READ cancelling NOTIFY operationChanged)
    Q_PROPERTY(QString progressTitle READ progressTitle NOTIFY operationChanged)
    Q_PROPERTY(QString progressTask READ progressTask NOTIFY operationChanged)
    Q_PROPERTY(double progressValue READ progressValue NOTIFY operationChanged)
    Q_PROPERTY(QString progressLog READ progressLog NOTIFY operationChanged)
    Q_PROPERTY(QVariantList exportFilters READ exportFilters NOTIFY exportFiltersChanged)
    Q_PROPERTY(QStringList exportEncodings READ exportEncodings CONSTANT)
    Q_PROPERTY(QString exportEncoding READ exportEncoding NOTIFY exportFiltersChanged)
public:
    AutomationViewModel(AssFile &file, SubtitleGridModel &grid, DocumentViewModel &document,
                        VideoViewModel &video, AudioViewModel &audio, TranslationService &i18n);
    ~AutomationViewModel() override;

    QVariantList scripts() const;
    QVariantList menu() const;
    QUrl lastFolder() const;
    Q_INVOKABLE void addScripts(const QList<QUrl> &urls);
    Q_INVOKABLE void removeScript(int index);
    Q_INVOKABLE void reloadScript(int index);
    Q_INVOKABLE void reloadAutoload();
    Q_INVOKABLE void validateMenu();
    Q_INVOKABLE bool runMacro(int id);
    Q_INVOKABLE QString scriptInfo(int index) const;
    bool busy() const;
    bool progressVisible() const;
    bool cancelling() const;
    QString progressTitle() const;
    QString progressTask() const;
    double progressValue() const;
    QString progressLog() const;
    QVariantList exportFilters() const;
    QStringList exportEncodings() const;
    QString exportEncoding() const;
    Q_INVOKABLE void cancelOperation();
    Q_INVOKABLE void completeDialog(const QVariant &button, const QVariantMap &values);
    Q_INVOKABLE void completeFile(const QList<QUrl> &urls);
    Q_INVOKABLE bool configureExportFilter(int id);
    Q_INVOKABLE bool exportSubtitles(const QUrl &path, const QString &encoding, const QVariantList &orderedIds);

signals:
    void operationChanged();
    void operationFinished(bool success);
    void dialogRequested(const QVariantMap &request);
    void fileRequested(const QVariantMap &request);
    void exportFiltersChanged();
    void scriptsChanged();
    void menuChanged();
    void executionError(const QString &message);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void refresh();
    void rebuildMenu(bool validate);
    void pollOperation();
    bool reportError(const QString &name, const QString &error);
};

} // namespace aegisub_qt::automation
