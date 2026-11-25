#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

class LogViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString text READ text NOTIFY textChanged)

public:
    explicit LogViewModel(QObject *parent = nullptr);

    QString text() const { return text_; }
    Q_INVOKABLE void setVisible(bool visible);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void clear();
    Q_INVOKABLE void copy();

signals:
    void textChanged();

private:
    QString text_;
    quint64 revision_ = 0;
    QTimer timer_;
};
