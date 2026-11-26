#include "fold_state.h"

#include "../ass_dialogue.h"
#include "../ass_file.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace {
constexpr char fold_key[] = "_aegi_folddata";

std::string markerValue(bool end, bool collapsed, int id) {
    std::string value = "0;0;";
    value[0] = end ? '1' : '0';
    value[2] = collapsed ? '1' : '0';
    return value + std::to_string(id);
}

bool inRange(int index, std::size_t size) {
    return index >= 0 && static_cast<std::size_t>(index) < size;
}
} // namespace

FoldState::Marker FoldState::parseMarker(std::string_view value) {
    Marker result;
    result.exists = true;
    if (value.size() < 5 || value[1] != ';' || value[3] != ';' ||
        (value[0] != '0' && value[0] != '1') || (value[2] != '0' && value[2] != '1'))
        return result;

    auto first = value.data() + 4;
    auto last = value.data() + value.size();
    // Native integer parsing also accepts an explicit positive sign.
    if (*first == '+') {
        ++first;
        if (first == last || *first < '0' || *first > '9')
            return result;
    }
    auto parsed = std::from_chars(first, last, result.raw_id);
    if (parsed.ec != std::errc{} || parsed.ptr != last)
        return result;

    result.parsed = true;
    result.end = value[0] == '1';
    result.collapsed = value[2] == '1';
    result.id = result.raw_id;
    return result;
}

void FoldState::rebuild(AssFile const& file, std::vector<AssDialogue*> const& docRows) {
    doc_rows_ = docRows;
    rows_.assign(docRows.size(), Row{});
    markers_.assign(docRows.size(), Marker{});
    representatives_.resize(docRows.size());
    folds_.clear();
    max_depth_ = 0;
    has_markers_ = false;

    // Resolve exact extradata references, including on malformed input with
    // stale IDs. Reserve even unreferenced fold IDs (e.g. clipboard data).
    std::unordered_map<std::uint32_t, Marker> entries;
    std::unordered_set<int> usedIds;
    std::int64_t largestId = 0;
    for (auto const& entry : file.Extradata) {
        if (entry.key != fold_key)
            continue;
        auto marker = parseMarker(entry.value);
        entries.emplace(entry.id, marker);
        if (marker.parsed) {
            usedIds.insert(marker.id);
            largestId = std::max(largestId, std::int64_t{marker.id});
        }
    }

    constexpr auto maxId = (std::numeric_limits<int>::max)();
    std::int64_t candidate = largestId == maxId ? 1 : largestId + 1;
    auto allocateId = [&]() -> int {
        for (std::int64_t attempts = 0; attempts < maxId; ++attempts) {
            auto id = static_cast<int>(candidate);
            candidate = candidate == maxId ? 1 : candidate + 1;
            if (usedIds.insert(id).second)
                return id;
        }
        return 0;
    };

    // Each original ID has one running generation. A completed pair or an
    // unmatched closer ends that generation; subsequent endpoints behave as
    // a fresh ID, just as in the native remap, without traversing a remap chain.
    struct Generation {
        int id;
        int opener = -1;
        bool finished = false;
    };
    std::unordered_map<int, Generation> generations;
    std::vector<int> stack;
    std::vector<int> endAt(docRows.size(), -1);
    auto count = static_cast<int>(docRows.size());
    for (int i = 0; i < count; ++i) {
        if (!docRows[i])
            continue;
        auto& marker = markers_[i];
        for (auto extraId : docRows[i]->ExtradataIds.get()) {
            auto it = entries.find(extraId);
            if (it != entries.end()) {
                marker = it->second;
                has_markers_ = true;
                break;
            }
        }
        if (!marker.parsed)
            continue;

        auto& state = generations.try_emplace(marker.raw_id, Generation{marker.raw_id}).first->second;
        if (state.finished) {
            int id = allocateId();
            if (!id)
                continue;
            state = Generation{id};
        }
        marker.id = state.id;
        if (!marker.end) {
            if (state.opener == -1) {
                state.opener = i;
                stack.push_back(i);
            }
        } else if (state.opener == -1) {
            state.finished = true;
        } else {
            int opener = state.opener;
            // Resolve crossings as native: keep this pair and discard the
            // still-open starters inside it. Each starter is popped at most once.
            while (stack.back() != opener) {
                auto& crossed = generations.at(markers_[stack.back()].raw_id);
                crossed.finished = true;
                stack.pop_back();
            }
            stack.pop_back();
            state.finished = true;
            endAt[opener] = i;
        }
    }
    next_id_ = allocateId();

    // Only survived pairs contribute to depth, containment and visibility.
    stack.clear();
    int hiddenBy = -1;
    for (int i = 0; i < count; ++i) {
        auto& row = rows_[i];
        row.parent = stack.empty() ? -1 : stack.back();
        row.innermost = row.parent;
        representatives_[i] = hiddenBy < 0 ? i : folds_[hiddenBy].start;
        if (endAt[i] != -1) {
            auto const& marker = markers_[i];
            row.start = true;
            row.innermost = static_cast<int>(folds_.size());
            folds_.push_back(Fold{i, endAt[i], row.parent, marker.id, marker.collapsed});
            stack.push_back(row.innermost);
            if (hiddenBy < 0 && marker.collapsed)
                hiddenBy = row.innermost;
        }
        row.depth = static_cast<int>(stack.size());
        max_depth_ = std::max(max_depth_, row.depth);
        if (!stack.empty() && folds_[stack.back()].end == i) {
            row.end = true;
            if (hiddenBy == stack.back())
                hiddenBy = -1;
            stack.pop_back();
        }
    }
}

FoldState::Row const* FoldState::row(int docRow) const {
    return inRange(docRow, rows_.size()) ? &rows_[docRow] : nullptr;
}

FoldState::Fold const* FoldState::fold(int index) const {
    return inRange(index, folds_.size()) ? &folds_[index] : nullptr;
}

void FoldState::buildMappings(std::vector<int>& visibleToDoc, std::vector<int>& docToVisible) const {
    visibleToDoc.clear();
    visibleToDoc.reserve(rows_.size());
    docToVisible.assign(rows_.size(), -1);
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
        if (representatives_[i] != i)
            continue;
        docToVisible[i] = static_cast<int>(visibleToDoc.size());
        visibleToDoc.push_back(i);
    }
}

int FoldState::visibleRepresentative(int docRow) const {
    return inRange(docRow, representatives_.size()) ? representatives_[docRow] : -1;
}

bool FoldState::canCreate(int start, int end) const {
    if (start >= end || !row(start) || !row(end) || !doc_rows_[start] || !doc_rows_[end] || !next_id_)
        return false;
    auto const& first = rows_[start];
    auto const& last = rows_[end];
    // Between non-delimiters with the same enclosing fold, any intervening
    // folds must be wholly contained by the new one.
    return !first.start && !first.end && !last.start && !last.end && first.parent == last.parent;
}

bool FoldState::create(AssFile& file, int start, int end, bool collapsed) const {
    if (!canCreate(start, end))
        return false;
    return writeChanges(file, {{start, markerValue(false, collapsed, next_id_)},
                               {end, markerValue(true, collapsed, next_id_)}});
}

bool FoldState::setCollapsed(AssFile& file, int foldIndex, bool collapsed) const {
    return setCollapsed(file, std::vector<int>{foldIndex}, collapsed);
}

bool FoldState::setCollapsed(AssFile& file, std::vector<int> const& foldIndices, bool collapsed) const {
    return updateCollapsed(file, foldIndices, collapsed);
}

bool FoldState::toggleCollapsed(AssFile& file, std::vector<int> const& foldIndices) const {
    return updateCollapsed(file, foldIndices, std::nullopt);
}

bool FoldState::updateCollapsed(AssFile& file, std::vector<int> const& foldIndices, std::optional<bool> collapsed) const {
    std::vector<std::pair<int, std::string>> changes;
    for (int index : foldIndices) {
        auto target = fold(index);
        if (!target)
            continue;
        bool desired = collapsed.value_or(!target->collapsed);
        if (target->collapsed != desired || markers_[target->end].collapsed != desired) {
            changes.emplace_back(target->start, markerValue(false, desired, target->id));
            changes.emplace_back(target->end, markerValue(true, desired, target->id));
        }
    }
    return writeChanges(file, std::move(changes));
}

bool FoldState::setAllCollapsed(AssFile& file, bool collapsed) const {
    std::vector<int> indices;
    indices.reserve(folds_.size());
    for (int i = 0; i < static_cast<int>(folds_.size()); ++i)
        indices.push_back(i);
    return setCollapsed(file, indices, collapsed);
}

bool FoldState::clear(AssFile& file, int foldIndex) const {
    return clear(file, std::vector<int>{foldIndex});
}

bool FoldState::clear(AssFile& file, std::vector<int> const& foldIndices) const {
    return clearMatching(file, foldIndices, {});
}

bool FoldState::clearAtRows(AssFile& file, std::vector<int> const& docRows) const {
    std::vector<int> indices;
    for (int doc : docRows) {
        auto info = row(doc);
        if (info && info->innermost >= 0)
            indices.push_back(info->innermost);
    }
    return clearMatching(file, indices, docRows);
}

bool FoldState::clearMatching(AssFile& file, std::vector<int> const& foldIndices, std::vector<int> const& docRows) const {
    std::vector<std::pair<int, std::string>> changes;
    std::unordered_set<int> clearedIds;
    for (int index : foldIndices) {
        if (auto target = fold(index)) {
            changes.emplace_back(target->start, std::string{});
            changes.emplace_back(target->end, std::string{});
            clearedIds.insert(target->id);
        }
    }
    for (int doc : docRows) {
        if (row(doc) && markers_[doc].exists)
            changes.emplace_back(doc, std::string{});
    }
    // Include duplicate starters belonging to the generations being cleared.
    if (!clearedIds.empty()) {
        for (int i = 0; i < static_cast<int>(markers_.size()); ++i) {
            if (markers_[i].parsed && clearedIds.count(markers_[i].id))
                changes.emplace_back(i, std::string{});
        }
    }
    return writeChanges(file, std::move(changes));
}

bool FoldState::clearAll(AssFile& file) const {
    std::vector<std::pair<int, std::string>> changes;
    for (int i = 0; i < static_cast<int>(markers_.size()); ++i) {
        if (markers_[i].exists)
            changes.emplace_back(i, std::string{});
    }
    return writeChanges(file, std::move(changes), false);
}

bool FoldState::writeChanges(AssFile& file, std::vector<std::pair<int, std::string>> changes, bool normalize) const {
    if (changes.empty())
        return false;

    // Materialize generation splits on explicit writes, never during reads.
    // Otherwise removing a pair (especially a crossing) can join unrelated
    // invalid endpoints back together under their shared original ID.
    std::vector<std::string const*> desired(rows_.size());
    for (auto const& change : changes)
        desired[change.first] = &change.second;
    std::vector<std::string> remapped;
    if (normalize) {
        auto size = std::count_if(markers_.begin(), markers_.end(), [](Marker const& marker) {
            return marker.id != marker.raw_id;
        });
        remapped.reserve(size);
        for (int i = 0; i < static_cast<int>(markers_.size()); ++i) {
            auto const& marker = markers_[i];
            if (desired[i] || !marker.parsed || marker.id == marker.raw_id)
                continue;
            auto target = (rows_[i].start || rows_[i].end) ? fold(rows_[i].innermost) : nullptr;
            remapped.push_back(markerValue(marker.end, target ? target->collapsed : marker.collapsed, marker.id));
            desired[i] = &remapped.back();
        }
    }

    // Batch exact-key replacement. The native helper scans all extradata to
    // insert a value and its enumeration assumes non-dangling, sorted IDs.
    // Index the data once so bulk edits are linear and only remove fold IDs.
    std::unordered_set<std::uint32_t> foldEntries;
    std::unordered_map<std::string, std::uint32_t> values;
    std::uint64_t nextExtra = file.next_extradata_id;
    for (auto const& entry : file.Extradata) {
        nextExtra = std::max(nextExtra, std::uint64_t{entry.id} + 1);
        if (entry.key == fold_key) {
            foldEntries.insert(entry.id);
            values.emplace(entry.value, entry.id);
        }
    }

    // Do not assign a new value to an existing dangling reference.
    std::unordered_set<std::uint32_t> danglingIds;
    for (auto dialogue : doc_rows_) {
        if (dialogue) {
            for (auto id : dialogue->ExtradataIds.get()) {
                if (id >= nextExtra)
                    danglingIds.insert(id);
            }
        }
    }

    std::vector<ExtradataEntry> additions;
    std::vector<std::pair<AssDialogue*, std::vector<std::uint32_t>>> replacements;
    constexpr auto maxExtraId = (std::numeric_limits<std::uint32_t>::max)();
    for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
        if (!desired[i])
            continue;
        auto& dialogue = *doc_rows_[i];
        auto const& previous = dialogue.ExtradataIds.get();
        std::vector<std::uint32_t> ids;
        for (auto id : previous) {
            if (!foldEntries.count(id))
                ids.push_back(id);
        }
        auto const& value = *desired[i];
        if (!value.empty()) {
            auto it = values.find(value);
            if (it == values.end()) {
                while (nextExtra < maxExtraId && danglingIds.count(static_cast<std::uint32_t>(nextExtra)))
                    ++nextExtra;
                if (nextExtra >= maxExtraId)
                    return false;
                auto id = static_cast<std::uint32_t>(nextExtra++);
                additions.push_back(ExtradataEntry{id, 0, fold_key, value});
                it = values.emplace(value, id).first;
            }
            if (std::is_sorted(ids.begin(), ids.end()))
                ids.insert(std::lower_bound(ids.begin(), ids.end(), it->second), it->second);
            else
                ids.push_back(it->second);
        }
        if (ids != previous)
            replacements.emplace_back(&dialogue, std::move(ids));
    }
    if (replacements.empty())
        return false;
    if (!additions.empty()) {
        file.Extradata.reserve(file.Extradata.size() + additions.size());
        for (auto& entry : additions)
            file.Extradata.push_back(std::move(entry));
        file.next_extradata_id = static_cast<std::uint32_t>(nextExtra);
    }
    for (auto& replacement : replacements)
        replacement.first->ExtradataIds = std::move(replacement.second);
    return true;
}
