#include "audio_waveform_item.h"

#include "viewmodels/audio_vm.h"

#include <QColor>
#include <QFont>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kRulerHeight = 20;
constexpr double kGrabRadiusPx = 6.0;

QColor blend(const QColor &a, const QColor &b, float t) {
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

QString formatTick(double milliseconds, double step_ms) {
    const int ms = int(std::max(0.0, milliseconds));
    const int total_seconds = ms / 1000;
    const int minutes = total_seconds / 60;
    const int seconds = total_seconds % 60;
    if (total_seconds >= 3600) {
        const int hours = total_seconds / 3600;
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes % 60, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }
    if (step_ms < 1000.0) {
        return QStringLiteral("%1:%2.%3")
            .arg(minutes)
            .arg(seconds, 2, 10, QLatin1Char('0'))
            .arg((ms % 1000) / 100);
    }
    return QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
}

} // namespace

AudioWaveformItem::AudioWaveformItem(QQuickItem *parent)
: QQuickPaintedItem(parent)
{
    setOpaquePainting(true);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton);
}

QObject *AudioWaveformItem::viewModel() const { return vm_; }

void AudioWaveformItem::setViewModel(QObject *view_model) {
    AudioViewModel *vm = qobject_cast<AudioViewModel *>(view_model);
    if (vm_ == vm)
        return;

    if (vm_)
        disconnect(vm_, nullptr, this, nullptr);
    vm_ = vm;

    if (vm_) {
        // Only audio or viewport changes can require new peaks/spectrum.
        // Ordinary playhead ticks just repaint the overlay.
        connect(vm_, &AudioViewModel::audioChanged, this, [this] {
            invalidatePeaks();
            update();
        });
        connect(vm_, &AudioViewModel::viewChanged, this, [this] {
            polish();
            update();
        });
        connect(vm_, &AudioViewModel::spectrumModeChanged, this, [this] { polish(); update(); });
        connect(vm_, &AudioViewModel::activeTimesChanged, this, [this] { update(); });
        connect(vm_, &AudioViewModel::playbackTimeChanged, this, [this] { update(); });
        connect(vm_, &AudioViewModel::playingChanged, this, [this] { update(); });
        connect(vm_, &AudioViewModel::keyframeTimesChanged, this, [this] { update(); });
        connect(vm_, &AudioViewModel::snapToKeyframesChanged, this, [this] { update(); });

        vm_->setViewportWidthPx(width());
    }

    invalidatePeaks();
    update();
    emit viewModelChanged();
}

void AudioWaveformItem::geometryChange(const QRectF &new_geometry,
                                       const QRectF &old_geometry) {
    QQuickPaintedItem::geometryChange(new_geometry, old_geometry);
    if (new_geometry.width() != old_geometry.width()) {
        if (vm_)
            vm_->setViewportWidthPx(new_geometry.width());
        polish();
    } else if (old_geometry.height() <= kRulerHeight && new_geometry.height() > kRulerHeight) {
        polish();
    }
}

double AudioWaveformItem::xToMs(double x) const {
    if (!vm_)
        return 0.0;
    const double zoom = std::max(1.0, vm_->zoomPxPerSec());
    return vm_->viewStartMs() + x * 1000.0 / zoom;
}

double AudioWaveformItem::msToX(double ms) const {
    if (!vm_)
        return 0.0;
    const double zoom = std::max(1.0, vm_->zoomPxPerSec());
    return (ms - vm_->viewStartMs()) * zoom / 1000.0;
}

int AudioWaveformItem::visibleMs() const {
    return vm_ ? vm_->visibleMs() : 1000;
}

int AudioWaveformItem::snappedMs(int ms, Qt::KeyboardModifiers modifiers) const {
    if (!vm_ || !vm_->snapToKeyframes())
        return ms;
    if (modifiers.testFlag(Qt::ShiftModifier))
        return ms;

    const double zoom = std::max(1.0, vm_->zoomPxPerSec());
    const int radius = vm_->snapDistancePx();
    if (radius == 0)
        return ms;
    const int tolerance_ms = int(std::floor(radius * 1000.0 / zoom));
    const int nearest = vm_->nearestKeyframeMs(ms, tolerance_ms);
    return nearest >= 0 ? nearest : ms;
}

void AudioWaveformItem::setColors(const QVariantMap &colors) {
    if (colors_ == colors) return;
    colors_ = colors;
    recolorSpectrum();
    update();
    emit colorsChanged();
}

void AudioWaveformItem::recolorSpectrum() {
    if (spectrum_.isNull()) return;
    const auto low = colors_.value("background").value<QColor>();
    const auto mid = colors_.value("primary").value<QColor>();
    const auto high = colors_.value("foreground").value<QColor>();
    QList<QRgb> table;
    table.reserve(256);
    for (int i = 0; i < 256; ++i)
        table.append((i < 160 ? blend(low, mid, i / 160.f)
                              : blend(mid, high, (i - 160) / 95.f)).rgb());
    spectrum_.setColorTable(table);
}

void AudioWaveformItem::invalidatePeaks() {
    peaks_valid_ = false;
    spectrum_ = {};
    polish();
}

void AudioWaveformItem::updatePolish() {
    if (!vm_ || !vm_->hasAudio()) return;
    if (vm_->spectrumMode()) refreshSpectrum();
    else refreshPeaks();
    update();
}

void AudioWaveformItem::refreshSpectrum() {
    if (width() < 1 || height() <= kRulerHeight) return;
    constexpr int bins = aegisub_qt::AudioBridge::spectrumBins;
    const int columns = std::clamp(int(std::ceil(width())), 1, 2048);
    const int start = vm_->viewStartMs();
    const auto end = vm_->msToSample(start + visibleMs());
    const auto first = vm_->msToSample(start);
    const auto needed = std::min<int64_t>(end + bins, vm_->msToSample(vm_->durationMs()));
    const auto decoded = std::min(vm_->decodedSamples(), needed);
    if (decoded < needed && !retry_pending_) {
        retry_pending_ = true;
        QTimer::singleShot(250, this, [this] { retry_pending_ = false; polish(); });
    }
    if (spectrum_.width() == columns && first == spectrum_start_ && end == spectrum_end_
        && decoded == spectrum_decoded_) return;
    spectrum_start_ = first;
    spectrum_end_ = end;
    spectrum_decoded_ = decoded;
    std::vector<float> levels;
    vm_->computeSpectrum(first, end, columns, levels);
    if (levels.size() != size_t(columns) * bins) {
        spectrum_ = {};
        return;
    }
    spectrum_ = QImage(columns, bins, QImage::Format_Indexed8);
    if (spectrum_.isNull()) return;
    // A logarithmic vertical scale gives speech and the 440 Hz test tone room.
    for (int y = 0; y < bins; ++y) {
        const auto binAt = [bins](double row) {
            return std::pow(double(bins), 1.0 - row / (bins - 1)) - 1.0;
        };
        const int low = std::clamp(int(std::lround(binAt(y + 0.5))), 0, bins - 1);
        const int high = std::clamp(int(std::lround(binAt(y - 0.5))), low, bins - 1);
        auto row = spectrum_.scanLine(y);
        for (int x = 0; x < columns; ++x) {
            auto base = levels.data() + size_t(x) * bins;
            row[x] = uchar(std::lround(*std::max_element(base + low, base + high + 1) * 255.0));
        }
    }
    recolorSpectrum();

}

void AudioWaveformItem::refreshPeaks() {
    if (!vm_ || !vm_->hasAudio()) {
        peaks_valid_ = false;
        return;
    }

    const int columns = int(width());
    if (columns <= 0)
        return;

    const int view_start = vm_->viewStartMs();
    const double zoom = vm_->zoomPxPerSec();
    if (peaks_valid_ && peaks_width_ == columns && peaks_view_start_ == view_start
        && qFuzzyCompare(peaks_zoom_, zoom)) {
        return;
    }

    const int end_ms = view_start + visibleMs();
    vm_->computePeaks(vm_->msToSample(view_start), vm_->msToSample(end_ms), columns,
                      peak_min_, peak_max_);
    peaks_valid_ = true;
    peaks_width_ = columns;
    peaks_view_start_ = view_start;
    peaks_zoom_ = zoom;
}

void AudioWaveformItem::paint(QPainter *painter) {
    const double w = width();
    const double h = height();
    const auto color = [this](const char *key) { return colors_.value(QLatin1String(key)).value<QColor>(); };
    const auto kBackground = color("background"), kRulerBackground = color("surface");
    const auto kTickMajor = color("outline"), kTickText = color("muted");
    const auto kWaveOutside = color("secondary"), kWaveInside = color("primary");
    auto kSelection = kWaveInside;
    kSelection.setAlpha(40);
    const auto kStartLine = color("start"), kEndLine = color("end");
    const auto kPlayhead = color("foreground"), kKeyframeLine = color("keyframe");
    painter->fillRect(QRectF(0, 0, w, h), kBackground);
    if (w <= 0 || h <= 0)
        return;

    if (!vm_ || !vm_->hasAudio()) {
        painter->setPen(kTickText);
        painter->drawText(QRectF(0, 0, w, h), Qt::AlignCenter, "No audio loaded");
        return;
    }

    int start_ms = vm_->activeStartMs();
    int end_ms = vm_->activeEndMs();
    if (drag_edge_ != DragEdge::None) {
        start_ms = preview_start_ms_;
        end_ms = preview_end_ms_;
    }

    // ---- Time ruler ------------------------------------------------------
    painter->fillRect(QRectF(0, 0, w, kRulerHeight), kRulerBackground);

    const double zoom = std::max(1.0, vm_->zoomPxPerSec());
    const double view_start = vm_->viewStartMs();
    const double seconds_visible = w * 1000.0 / zoom;

    static const double candidates[] = {100, 200, 500, 1000, 2000, 5000, 10000,
                                        15000, 30000, 60000, 120000, 300000,
                                        600000, 1800000, 3600000};
    double step_ms = 3600000;
    for (double candidate : candidates) {
        if (candidate * zoom / 1000.0 >= 70.0) {
            step_ms = candidate;
            break;
        }
    }

    QFont ruler_font = painter->font();
    ruler_font.setPixelSize(9);
    painter->setFont(ruler_font);

    const double first_tick = std::floor(view_start / step_ms) * step_ms;
    for (double tick = first_tick; tick <= view_start + seconds_visible; tick += step_ms) {
        if (tick < 0)
            continue;
        const double x = (tick - view_start) * zoom / 1000.0;
        if (x < 0 || x > w)
            continue;
        painter->setPen(kTickMajor);
        painter->drawLine(QPointF(x, 0), QPointF(x, kRulerHeight));
        painter->setPen(kTickText);
        painter->drawText(QRectF(x + 3, 2, 110, kRulerHeight - 2),
                          Qt::AlignLeft | Qt::AlignVCenter, formatTick(tick, step_ms));
    }

    // Waveform and spectrogram share the same time coordinates and overlays.
    const bool show_spectrum = vm_->spectrumMode();

    const double wave_top = kRulerHeight;
    const double wave_height = h - kRulerHeight;
    const double center_y = wave_top + wave_height / 2.0;
    const double amplitude = std::max(1.0, wave_height / 2.0 - 2.0);

    const double start_x = msToX(start_ms);
    const double end_x = msToX(end_ms);

    if (show_spectrum && !spectrum_.isNull())
        painter->drawImage(QRectF(0, wave_top, w, wave_height), spectrum_);
    painter->fillRect(QRectF(std::clamp(start_x, 0.0, w), wave_top,
                             std::clamp(end_x, 0.0, w) - std::clamp(start_x, 0.0, w),
                             wave_height),
                      kSelection);

    if (!show_spectrum) {
        painter->setPen(kTickMajor);
        painter->drawLine(QPointF(0, center_y), QPointF(w, center_y));
    }

    if (!show_spectrum && peaks_valid_) {
        const int columns = std::min<int>(peaks_width_, int(peak_min_.size()));
        QPen outside_pen(kWaveOutside);
        outside_pen.setWidthF(1.0);
        QPen inside_pen(kWaveInside);
        inside_pen.setWidthF(1.0);
        bool inside = false;
        for (int x = 0; x < columns; ++x) {
            const bool selected = x >= int(start_x) && x <= int(end_x);
            if (selected != inside) {
                painter->setPen(selected ? inside_pen : outside_pen);
                inside = selected;
            }
            const double low = std::clamp(double(peak_min_[size_t(x)]), -1.0, 1.0);
            const double high = std::clamp(double(peak_max_[size_t(x)]), -1.0, 1.0);
            double y_top = center_y - high * amplitude;
            double y_bottom = center_y - low * amplitude;
            if (y_bottom - y_top < 1.0)
                y_bottom = y_top + 1.0;
            painter->drawLine(QPointF(x + 0.5, y_top), QPointF(x + 0.5, y_bottom));
        }
    }

    // ---- Keyframes -------------------------------------------------------
    // Only the keyframes inside the visible range are drawn; the list is sorted
    // so a binary search finds the first visible one in O(log N).
    if (vm_->keyframeCount() > 0) {
        const auto &keyframes = vm_->keyframeTimesMs();
        const int view_begin = vm_->viewStartMs();
        const int view_end = view_begin + visibleMs();
        auto it = std::lower_bound(keyframes.begin(), keyframes.end(), view_begin);
        QPen keyframe_pen(kKeyframeLine);
        keyframe_pen.setWidthF(1.0);
        keyframe_pen.setCosmetic(true);
        painter->setPen(keyframe_pen);
        for (; it != keyframes.end() && *it <= view_end; ++it) {
            const double x = msToX(*it);
            if (x < 0 || x > w)
                continue;
            painter->drawLine(QPointF(x, kRulerHeight), QPointF(x, h));
        }
    }

    // ---- Playhead --------------------------------------------------------
    if (vm_->isPlaying()) {
        const double x = msToX(vm_->playbackTimeMs());
        if (x >= 0 && x <= w) {
            QPen pen(kPlayhead);
            pen.setWidthF(1.0);
            painter->setPen(pen);
            painter->drawLine(QPointF(x, 0), QPointF(x, h));
        }
    }

    // ---- Start / End lines ----------------------------------------------
    QPen start_pen(kStartLine);
    start_pen.setWidthF(2.0);
    painter->setPen(start_pen);
    if (start_x >= 0 && start_x <= w)
        painter->drawLine(QPointF(start_x, kRulerHeight), QPointF(start_x, h));

    QPen end_pen(kEndLine);
    end_pen.setWidthF(2.0);
    painter->setPen(end_pen);
    if (end_x >= 0 && end_x <= w)
        painter->drawLine(QPointF(end_x, kRulerHeight), QPointF(end_x, h));

}

void AudioWaveformItem::mousePressEvent(QMouseEvent *event) {
    if (!vm_ || !vm_->hasAudio()) {
        QQuickPaintedItem::mousePressEvent(event);
        return;
    }

    const double x = event->position().x();
    const double ms = std::clamp(xToMs(x), 0.0, double(std::max(1, vm_->durationMs())));
    const int duration = std::max(1, vm_->durationMs());
    const int clicked_ms = int(std::clamp(ms, 0.0, double(duration)));
    // Snapping only affects the value that will be written, not the hit test
    // against the existing red/blue lines.
    const int snapped_clicked_ms = snappedMs(clicked_ms, event->modifiers());

    const int active_start = vm_->activeStartMs();
    const int active_end = vm_->activeEndMs();
    const double grab_ms = kGrabRadiusPx * 1000.0 / std::max(1.0, vm_->zoomPxPerSec());
    const bool near_start = std::abs(clicked_ms - active_start) <= grab_ms;
    const bool near_end = std::abs(clicked_ms - active_end) <= grab_ms;

    preview_start_ms_ = active_start;
    preview_end_ms_ = active_end;

    if (event->button() == Qt::RightButton) {
        drag_edge_ = DragEdge::End;
        preview_end_ms_ = std::max(snapped_clicked_ms, preview_start_ms_);
    } else if (event->button() == Qt::LeftButton) {
        if (near_start) {
            drag_edge_ = DragEdge::Start;
        } else if (near_end) {
            drag_edge_ = DragEdge::End;
        } else {
            // Clicking empty space sets the new start; clamping against the end
            // keeps the range ordered (same feel as the wxWidgets audio display).
            drag_edge_ = DragEdge::Start;
            preview_start_ms_ = std::min(snapped_clicked_ms, preview_end_ms_);
        }
    } else {
        QQuickPaintedItem::mousePressEvent(event);
        return;
    }

    event->accept();
    setKeepMouseGrab(true);
    update();
}

void AudioWaveformItem::mouseMoveEvent(QMouseEvent *event) {
    if (drag_edge_ == DragEdge::None || !vm_) {
        QQuickPaintedItem::mouseMoveEvent(event);
        return;
    }

    const int duration = std::max(1, vm_->durationMs());
    int ms = int(std::clamp(xToMs(event->position().x()), 0.0, double(duration)));
    ms = std::clamp(snappedMs(ms, event->modifiers()), 0, duration);

    if (drag_edge_ == DragEdge::Start)
        preview_start_ms_ = std::min(ms, preview_end_ms_);
    else
        preview_end_ms_ = std::max(ms, preview_start_ms_);

    event->accept();
    update();
}

void AudioWaveformItem::mouseReleaseEvent(QMouseEvent *event) {
    if (drag_edge_ == DragEdge::None || !vm_) {
        QQuickPaintedItem::mouseReleaseEvent(event);
        return;
    }

    const int start = preview_start_ms_;
    const int end = preview_end_ms_;
    drag_edge_ = DragEdge::None;
    setKeepMouseGrab(false);
    event->accept();
    update();

    // One gesture = one commit = one undo step.
    vm_->commitLineTiming(start, end);
}

void AudioWaveformItem::wheelEvent(QWheelEvent *event) {
    if (!vm_ || !vm_->hasAudio()) {
        QQuickPaintedItem::wheelEvent(event);
        return;
    }

    const int delta = event->angleDelta().y();
    if (delta == 0)
        return;

    if (event->modifiers().testFlag(Qt::ControlModifier) != vm_->wheelDefaultsToZoom()) {
        vm_->zoomBy(delta > 0 ? 1.25 : 0.8);
    } else {
        const int step = std::max(100, visibleMs() / 8);
        vm_->scrollByMs(delta > 0 ? -step : step);
    }

    event->accept();
}
