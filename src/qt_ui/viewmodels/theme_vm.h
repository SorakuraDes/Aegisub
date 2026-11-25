#pragma once

#include <libaegisub/signal.h>
#include <QColor>
#include <QObject>
#include <QVariantList>

class ThemeViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QColor seedColor READ seedColor WRITE setSeedColor NOTIFY themeChanged)
    Q_PROPERTY(bool darkMode READ darkMode WRITE setDarkMode NOTIFY themeChanged)
    Q_PROPERTY(QVariantList presetColors READ presetColors CONSTANT)
    Q_PROPERTY(int radiusSmall READ radiusSmall CONSTANT)
    Q_PROPERTY(int radiusMedium READ radiusMedium CONSTANT)
    Q_PROPERTY(int controlHeight READ controlHeight CONSTANT)
    Q_PROPERTY(int menuBarHeight READ menuBarHeight CONSTANT)
    Q_PROPERTY(int statusBarHeight READ statusBarHeight CONSTANT)

public:
    explicit ThemeViewModel(QObject *parent = nullptr);
    QColor seedColor() const { return seed_; }
    void setSeedColor(const QColor &color);
    bool darkMode() const { return dark_; }
    void setDarkMode(bool dark);
    QVariantList presetColors() const;
    int radiusSmall() const { return 4; }
    int radiusMedium() const { return 6; }
    int controlHeight() const { return 32; }
    int menuBarHeight() const { return 28; }
    int statusBarHeight() const { return 24; }

#define THEME_ROLE(name) \
    Q_PROPERTY(QColor name READ name NOTIFY themeChanged) \
    QColor name() const { return name##_; }
    THEME_ROLE(primary)
    THEME_ROLE(onPrimary)
    THEME_ROLE(primaryContainer)
    THEME_ROLE(onPrimaryContainer)
    THEME_ROLE(secondary)
    THEME_ROLE(secondaryContainer)
    THEME_ROLE(onSecondaryContainer)
    THEME_ROLE(surface)
    THEME_ROLE(surfaceContainerLow)
    THEME_ROLE(surfaceContainer)
    THEME_ROLE(surfaceContainerHigh)
    THEME_ROLE(surfaceContainerHighest)
    THEME_ROLE(onSurface)
    THEME_ROLE(onSurfaceVariant)
    THEME_ROLE(outline)
    THEME_ROLE(outlineVariant)
    THEME_ROLE(rowActive)
    THEME_ROLE(rowSelected)
    THEME_ROLE(rowAlt)
    THEME_ROLE(rowComment)
    THEME_ROLE(danger)
    THEME_ROLE(warning)
    THEME_ROLE(kfLine)
    THEME_ROLE(endLine)
#undef THEME_ROLE

signals:
    void themeChanged();

private:
    void refreshPalette();
    void save();
    QColor primary_, onPrimary_, primaryContainer_, onPrimaryContainer_;
    QColor secondary_, secondaryContainer_, onSecondaryContainer_;
    QColor surface_, surfaceContainerLow_, surfaceContainer_, surfaceContainerHigh_, surfaceContainerHighest_;
    QColor onSurface_, onSurfaceVariant_, outline_, outlineVariant_;
    QColor rowActive_, rowSelected_, rowAlt_, rowComment_, danger_, warning_, kfLine_, endLine_;
    QColor seed_{0x00, 0xbc, 0xd4};
    bool dark_ = true;
    QString settingsPath_;
    agi::signal::Connection darkModeSubscription_;
};
