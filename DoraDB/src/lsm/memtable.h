#pragma once

#include "common/types.h"
#include <functional>
#include <map>
#include <optional>
#include <vector>

// ============================================================
// MemTable — sorted in-memory key-value store (uses std::map)
//
// Backed by a red-black tree (std::map), same data structure
// as Lab05's RB-Tree. Keys are int, values are serialized rows.
// When the table exceeds a size threshold, it's flushed to an SSTable.
// ============================================================

static constexpr int MEMTABLE_MAX_ENTRIES = 1000;

struct MemEntry {
  std::vector<char> data;
  int size = 0;
  bool deleted = false; // tombstone marker
};

class MemTable {
public:
  // Insert or update a key
  void Put(int key, const char *data, int size);

  // Mark a key as deleted (tombstone)
  void Delete(int key);

  // Get value for key. Returns false if not found or deleted.
  bool Get(int key, char *out_data, int *out_size) const;

  // Check if key exists (including tombstones)
  bool Contains(int key) const;

  // Is this entry a tombstone?
  bool IsDeleted(int key) const;

  // Number of entries (including tombstones)
  int Size() const { return entries_.size(); }

  // Is the memtable full?
  bool IsFull() const { return (int)entries_.size() >= MEMTABLE_MAX_ENTRIES; }

  // Iterate all entries in sorted order
  void ForEach(std::function<void(int key, const MemEntry &entry)> fn) const;

  // Clear all entries (after flush to SSTable)
  void Clear();

private:
  std::map<int, MemEntry> entries_; // RB-tree (sorted by key)
};
