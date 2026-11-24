#pragma once

// libaegisub first: agi::signal has a `slots` member, which Qt's macros would
// mangle if a Qt header were parsed first (same rule as undo_manager.h).
#include <libaegisub/signal.h>

#include <QQuickPaintedItem>

#include <vector>

class AudioViewModel;

/// Interactive waveform widget for the audio panel.
///
/// Paints the time ruler, the waveform peaks, the active line's selection and
/// its red/blue boundary lines, plus the playback head. Left/right click and
/// drag edit the active line timing: dragging only previews the lines and the
/// single commit is made on mouse release, so one gesture = one undo step.
class AudioWaveformItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QObject *viewModel READ viewModel WRITE setViewModel NOTIFY viewModelChanged)

public:
    explicit AudioWaveformItem(QQuickItem *parent = nullptr);

    QObject *viewModel() const;
    void setViewModel(QObject *view_model);

    void paint(QPainter *painter) override;
    void geometryChange(const QRectF &new_geometry, const QRectF &old_geometry) override;

signals:
    void viewModelChanged();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    enum class DragEdge { None, Start, End };

    double xToMs(double x) const;
    double msToX(double ms) const;
    int visibleMs() const;
    /// Snap `ms` to the nearest video keyframe when snapping is enabled and
    /// Shift is not held (tolerance: 6 px converted to ms at the current zoom).
    int snappedMs(int ms, Qt::KeyboardModifiers modifiers) const;
    void refreshPeaks();
    void invalidatePeaks();

    AudioViewModel *vm_ = nullptr;
    DragEdge drag_edge_ = DragEdge::None;
    int preview_start_ms_ = 0;
    int preview_end_ms_ = 0;

    bool peaks_valid_ = false;
    int peaks_width_ = 0;
    int peaks_view_start_ = -1;
    double peaks_zoom_ = 0.0;
    std::vector<float> peak_min_;
    std::vector<float> peak_max_;
};
