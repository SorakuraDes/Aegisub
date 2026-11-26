#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class AssDialogue;
class AssFile;

// The subtitle grid owns both row mappings; this cache owns only fold topology.
class FoldState {
public:
    struct Fold {
        int start;
        int end;
        int parent;
        int id;
        bool collapsed;
    };

    struct Row {
        int depth = 0;
        int parent = -1;
        int innermost = -1;
        bool start = false;
        bool end = false;
    };

    // Endpoints are inclusive. Parent excludes a fold that opens at this row;
    // innermost includes it, making both delimiters target the same fold.
    void rebuild(AssFile const& file, std::vector<AssDialogue*> const& docRows);
    Row const* row(int docRow) const;
    Fold const* fold(int index) const;
    std::vector<Fold> const& folds() const { return folds_; }
    int maxDepth() const { return max_depth_; }
    bool hasMarkers() const { return has_markers_; }

    // Hidden rows map to -1. To reveal/anchor a hidden row, use the visible
    // document row returned by visibleRepresentative().
    void buildMappings(std::vector<int>& visibleToDoc, std::vector<int>& docToVisible) const;
    int visibleRepresentative(int docRow) const;

    // Mutations only edit extradata. The caller commits and rebuilds the cache.
    bool canCreate(int start, int end) const;
    bool create(AssFile& file, int start, int end, bool collapsed = true) const;
    bool setCollapsed(AssFile& file, int foldIndex, bool collapsed) const;
    bool setCollapsed(AssFile& file, std::vector<int> const& foldIndices, bool collapsed) const;
    bool setAllCollapsed(AssFile& file, bool collapsed) const;
    bool toggleCollapsed(AssFile& file, std::vector<int> const& foldIndices) const;
    bool clear(AssFile& file, int foldIndex) const;
    bool clear(AssFile& file, std::vector<int> const& foldIndices) const;
    bool clearAtRows(AssFile& file, std::vector<int> const& docRows) const;
    bool clearAll(AssFile& file) const;

private:
    struct Marker {
        bool exists = false;
        bool parsed = false;
        bool end = false;
        bool collapsed = false;
        int raw_id = 0;
        int id = 0;
    };

    std::vector<AssDialogue*> doc_rows_;
    std::vector<Row> rows_;
    std::vector<Fold> folds_;
    std::vector<Marker> markers_;
    std::vector<int> representatives_;
    int max_depth_ = 0;
    int next_id_ = 0;
    bool has_markers_ = false;

    static Marker parseMarker(std::string_view value);
    bool updateCollapsed(AssFile& file, std::vector<int> const& foldIndices, std::optional<bool> collapsed) const;
    bool writeChanges(AssFile& file, std::vector<std::pair<int, std::string>> changes, bool normalize = true) const;
    bool clearMatching(AssFile& file, std::vector<int> const& foldIndices, std::vector<int> const& docRows) const;
};
