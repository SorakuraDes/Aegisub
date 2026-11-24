#pragma once

#include <libaegisub/signal.h>
#include <QObject>
#include <QString>
#include <string>

class AssDialogue;
class AssFile;
class SubtitleGridModel;
class UndoManager;
class VideoViewModel;

class VisualToolsViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int activeTool READ activeTool WRITE setActiveTool NOTIFY activeToolChanged)
    Q_PROPERTY(int playResX READ playResX NOTIFY visualStateChanged)
    Q_PROPERTY(int playResY READ playResY NOTIFY visualStateChanged)
    Q_PROPERTY(bool hasActiveLine READ hasActiveLine NOTIFY visualStateChanged)
    Q_PROPERTY(double posX READ posX NOTIFY visualStateChanged)
    Q_PROPERTY(double posY READ posY NOTIFY visualStateChanged)
    Q_PROPERTY(bool hasExplicitPos READ hasExplicitPos NOTIFY visualStateChanged)
    Q_PROPERTY(int clipX1 READ clipX1 NOTIFY visualStateChanged)
    Q_PROPERTY(int clipY1 READ clipY1 NOTIFY visualStateChanged)
    Q_PROPERTY(int clipX2 READ clipX2 NOTIFY visualStateChanged)
    Q_PROPERTY(int clipY2 READ clipY2 NOTIFY visualStateChanged)
    Q_PROPERTY(bool hasRectClip READ hasRectClip NOTIFY visualStateChanged)
    Q_PROPERTY(QString hoverCoordText READ hoverCoordText NOTIFY hoverCoordTextChanged)

public:
    enum Tool { Standard, DragPos, RectClip };
    Q_ENUM(Tool)

    VisualToolsViewModel(AssFile *file, SubtitleGridModel *gridModel,
                         VideoViewModel *videoVM, UndoManager *undoManager);

    int activeTool() const { return active_tool_; }
    void setActiveTool(int tool);
    int playResX() const { return play_res_x_; }
    int playResY() const { return play_res_y_; }
    bool hasActiveLine() const { return has_active_line_; }
    double posX() const { return pos_x_; }
    double posY() const { return pos_y_; }
    bool hasExplicitPos() const { return has_explicit_pos_; }
    int clipX1() const { return clip_x1_; }
    int clipY1() const { return clip_y1_; }
    int clipX2() const { return clip_x2_; }
    int clipY2() const { return clip_y2_; }
    bool hasRectClip() const { return has_rect_clip_; }
    QString hoverCoordText() const { return hover_coord_text_; }

    bool beginDrag();
    void updatePos(double sx, double sy);
    void updateRectClip(int x1, int y1, int x2, int y2);
    void endDrag();
    void setHoverCoord(double sx, double sy);
    void clearHoverCoord();
    void handleDocumentReload();
    Q_INVOKABLE void clearRectClip();

signals:
    void activeToolChanged();
    void visualStateChanged();
    void hoverCoordTextChanged();
    void dragEnded();

private:
    AssDialogue *activeLine() const;
    void syncSelection();
    void refresh();
    void onFileCommitted(int type, const AssDialogue *line);
    void commitText(AssDialogue *line, std::string text, const char *description, bool amend);

    AssFile *file_;
    SubtitleGridModel *grid_model_;
    VideoViewModel *video_vm_;
    agi::signal::Connection commit_connection_;
    int active_tool_ = Standard;
    int play_res_x_ = 384, play_res_y_ = 288;
    bool has_active_line_ = false;
    double pos_x_ = 0, pos_y_ = 0;
    bool has_explicit_pos_ = false;
    int clip_x1_ = 0, clip_y1_ = 0, clip_x2_ = 384, clip_y2_ = 288;
    bool has_rect_clip_ = false;
    QString hover_coord_text_;
    bool dragging_ = false;
    bool committing_ = false;
    int drag_commit_id_ = -1;
};
