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

signals:
    void scriptsChanged();
    void menuChanged();
    void executionError(const QString &message);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void refresh();
    void rebuildMenu(bool validate);
};

} // namespace aegisub_qt::automation
