#pragma once

#include <QObject>
#include <QStringList>
#include <QVariant>

#include <functional>
#include <memory>

class QJsonObject;
class QQuickWindow;

class HotkeyViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(QStringList contexts READ contexts NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)

public:
    explicit HotkeyViewModel(QObject *parent = nullptr);
    ~HotkeyViewModel() override;

    void attachMainWindow(QQuickWindow *window);
    void registerCommand(const QString &id, const QString &context, std::function<void()> action,
                         std::function<bool()> enabled = {});
    void setSoftLineBreakHandler(std::function<void()> handler);

    int revision() const;
    QStringList contexts() const;
    QString error() const;
    bool recording() const;

    Q_INVOKABLE bool dispatch(const QString &id);
    Q_INVOKABLE QString menuShortcut(const QString &commandId, const QString &context) const;
    Q_INVOKABLE QVariantList rows(const QString &context, const QString &search) const;
    Q_INVOKABLE bool rebind(const QString &context, const QString &command, const QString &oldKey, const QString &newKey);
    Q_INVOKABLE bool restoreDefaults();
    Q_INVOKABLE bool beginRecording(QObject *window);
    Q_INVOKABLE void cancelRecording();

signals:
    void changed();
    void errorChanged();
    void recordingChanged();
    void recorded(const QString &key);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct State;
    std::unique_ptr<State> state_;
    bool fail(const QString &error);
    bool save(const QJsonObject &map);
    void touch();
    void finishRecording(const QString &key);
};
