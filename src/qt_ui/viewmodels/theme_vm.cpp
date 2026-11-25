#include "theme_vm.h"
#include "../aegisub_core.h"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <algorithm>
#include <cmath>

namespace {
struct RGB { double r, g, b; };
struct ChromaHue { double c, h; };

double linear(double v) {
    return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

double encoded(double v) {
    v = std::clamp(v, 0.0, 1.0);
    return v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
}

// OKLab's D65 matrices operate on linear sRGB, not gamma-encoded channels.
ChromaHue seedChromaHue(const QColor &color) {
    const double r = linear(color.redF()), g = linear(color.greenF()), b = linear(color.blueF());
    const double l = std::cbrt(0.4122214708*r + 0.5363325363*g + 0.0514459929*b);
    const double m = std::cbrt(0.2119034982*r + 0.6806995451*g + 0.1073969566*b);
    const double s = std::cbrt(0.0883024619*r + 0.2817188376*g + 0.6299787005*b);
    const double a = 1.9779984951*l - 2.4285922050*m + 0.4505937099*s;
    const double bb = 0.0259040371*l + 0.7827717662*m - 0.8086757660*s;
    return {std::hypot(a, bb), std::atan2(bb, a)};
}

QColor tone(double lightness, double chroma, double hue) {
    const double x = std::cos(hue), y = std::sin(hue);
    const auto rgb = [=](double c) -> RGB {
        const double a = c*x, b = c*y;
        const auto cube = [](double t) { return t*t*t; };
        const double l = cube(lightness + 0.3963377774*a + 0.2158037573*b);
        const double m = cube(lightness - 0.1055613458*a - 0.0638541728*b);
        const double s = cube(lightness - 0.0894841775*a - 1.2914855480*b);
        return {4.0767416621*l - 3.3077115913*m + 0.2309699292*s,
               -1.2684380046*l + 2.6097574011*m - 0.3413193965*s,
               -0.0041960863*l - 0.7034186147*m + 1.7076147010*s};
    };
    const auto inGamut = [](RGB v) {
        return std::min({v.r,v.g,v.b}) >= 0 && std::max({v.r,v.g,v.b}) <= 1;
    };
    RGB result = rgb(chroma);
    if (!inGamut(result)) {
        // Preserve perceptual lightness and hue; shrink only chroma to fit sRGB.
        double lo = 0, hi = chroma;
        for (int i = 0; i < 16; ++i) {
            const double mid = (lo + hi) * 0.5;
            if (inGamut(rgb(mid))) lo = mid;
            else hi = mid;
        }
        result = rgb(lo);
    }
    return QColor::fromRgbF(encoded(result.r), encoded(result.g), encoded(result.b));
}
} // namespace

ThemeViewModel::ThemeViewModel(QObject *parent) : QObject(parent) {
    const QString config = aegisub_qt::OptionsFilePath();
    if (!config.isEmpty()) {
        settingsPath_ = QFileInfo(config).dir().filePath(QStringLiteral("qt-theme.ini"));
        QSettings store(settingsPath_, QSettings::IniFormat);
        const QColor stored(store.value("Appearance/seedColor").toString());
        if (stored.isValid() && stored.alpha() == 255) seed_ = stored;
        const auto dark = store.value("Appearance/darkMode").toString();
        if (dark == QStringLiteral("false")) dark_ = false;
    }
    refreshPalette();
}

void ThemeViewModel::setSeedColor(const QColor &color) {
    if (!color.isValid() || color.alpha() != 255) return;
    const auto rgb = QColor::fromRgb(color.rgb());
    if (rgb == seed_) return;
    seed_ = rgb;
    refreshPalette();
    save();
    emit themeChanged();
}

void ThemeViewModel::setDarkMode(bool dark) {
    if (dark_ == dark) return;
    dark_ = dark;
    refreshPalette();
    save();
    emit themeChanged();
}

void ThemeViewModel::save() {
    if (settingsPath_.isEmpty()) return;
    QSettings store(settingsPath_, QSettings::IniFormat);
    store.setValue("Appearance/seedColor", seed_.name());
    store.setValue("Appearance/darkMode", dark_);
    store.sync();
    if (store.status() != QSettings::NoError)
        qWarning() << "[theme] Could not save appearance to" << settingsPath_;
}

QVariantList ThemeViewModel::presetColors() const {
    struct Swatch { const char *name, *color; };
    constexpr Swatch swatches[] = {{"Cyan", "#00BCD4"}, {"Blue", "#3B82F6"},
        {"Violet", "#8B5CF6"}, {"Sakura", "#EC4899"}, {"Mint", "#10B981"}, {"Amber", "#F59E0B"}};
    QVariantList list;
    for (const auto &s : swatches)
        list.append(QVariantMap{{"name", QString::fromLatin1(s.name)}, {"color", QString::fromLatin1(s.color)}});
    return list;
}

void ThemeViewModel::refreshPalette() {
    const auto seed = seedChromaHue(seed_);
    const auto role = [&](double dark, double light, double c) {
        return tone(dark_ ? dark : light, c, seed.h);
    };
    const double c = std::min(seed.c, 0.23), neutral = c * 0.08;
    primary_ = role(.81,.46,c);
    onPrimary_ = role(.19,.99,c*.12);
    primaryContainer_ = role(.35,.89,c*.65);
    onPrimaryContainer_ = role(.94,.22,c*.36);
    secondary_ = role(.78,.44,c*.45);
    secondaryContainer_ = role(.32,.91,c*.30);
    onSecondaryContainer_ = role(.91,.24,c*.27);
    surface_ = role(.17,.985,neutral);
    surfaceContainerLow_ = role(.20,.965,neutral);
    surfaceContainer_ = role(.23,.94,neutral);
    surfaceContainerHigh_ = role(.26,.915,neutral);
    surfaceContainerHighest_ = role(.30,.885,neutral);
    onSurface_ = role(.94,.21,neutral);
    onSurfaceVariant_ = role(.78,.42,neutral*2);
    outline_ = role(.62,.53,neutral*2);
    outlineVariant_ = role(.38,.79,neutral*2);
    rowActive_ = role(.39,.84,c*.82);
    rowSelected_ = role(.32,.90,c*.52);
    rowAlt_ = role(.22,.96,neutral);
    rowComment_ = role(.73,.43,c*.22);
    danger_ = tone(dark_ ? .78 : .45, .16, .48);
    warning_ = tone(dark_ ? .85 : .46, .14, 1.37);
    kfLine_ = tone(dark_ ? .79 : .47, .16, 5.33);
    endLine_ = tone(dark_ ? .78 : .48, .15, 4.45);
}
