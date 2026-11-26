#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <thread>

class AssFile;
class DocumentViewModel;

class FontsCollectorViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int action READ action WRITE setAction NOTIFY actionChanged)
    Q_PROPERTY(QString destination READ destination WRITE setDestination NOTIFY destinationChanged)
    Q_PROPERTY(bool hasScriptPath READ hasScriptPath NOTIFY hasScriptPathChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QString logText READ logText NOTIFY logChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(int collectedCount READ collectedCount NOTIFY statusChanged)
    Q_PROPERTY(int missingCount READ missingCount NOTIFY statusChanged)
    Q_PROPERTY(bool outputFailed READ outputFailed NOTIFY statusChanged)
public:
    FontsCollectorViewModel(AssFile *file, DocumentViewModel *document, QObject *parent = nullptr);
    ~FontsCollectorViewModel() override;
    int action() const { return action_; }
    QString destination() const { return destination_; }
    bool hasScriptPath() const;
    bool running() const { return running_; }
    QString logText() const { return log_; }
    QString statusText() const { return status_; }
    int collectedCount() const { return collected_; }
    int missingCount() const { return missing_; }
    bool outputFailed() const { return failed_; }
    void setAction(int action);
    void setDestination(const QString &destination);
    Q_INVOKABLE void start();
    Q_INVOKABLE void copyLog() const;
    Q_INVOKABLE QUrl browseFolder() const;
    Q_INVOKABLE void selectDestination(const QUrl &url);

signals:
    void actionChanged();
    void destinationChanged();
    void hasScriptPathChanged();
    void runningChanged();
    void logChanged();
    void statusChanged();

private:
    QString resolve(const QString &path) const;
    void finish(int collected, int missing, bool failed);
    AssFile *file_;
    DocumentViewModel *document_;
    int action_ = 0;
    QString destination_ = QStringLiteral("?script");
    bool running_ = false;
    QString log_;
    QString status_;
    int collected_ = 0;
    int missing_ = 0;
    bool failed_ = false;
    std::thread worker_;
};
