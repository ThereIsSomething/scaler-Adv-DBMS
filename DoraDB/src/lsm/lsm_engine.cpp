#include "lsm/lsm_engine.h"
#include "common/config.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>

LSMEngine::LSMEngine(const std::string &data_dir) : data_dir_(data_dir) {
  std::filesystem::create_directories(data_dir);
}

LSMEngine::~LSMEngine() = default;

std::string LSMEngine::SSTablePath(const std::string &table, int id) {
  return data_dir_ + "/" + table + "_sst_" + std::to_string(id) + ".sst";
}

void LSMEngine::CreateTable(const std::string &name, const Schema &schema) {
  tables_[name].schema = schema;
}

bool LSMEngine::Insert(const std::string &table, const Row &row) {
  auto it = tables_.find(table);
  if (it == tables_.end())
    return false;
  auto &ts = it->second;

  char buf[PAGE_SIZE];
  int size = SerializeRow(row, ts.schema, buf);
  int key = (ts.schema.pk_index >= 0) ? row[ts.schema.pk_index].int_val : 0;

  ts.memtable.Put(key, buf, size);
  MaybeFlush(table);
  return true;
}

std::vector<Row> LSMEngine::Get(const std::string &table, int key) {
  auto it = tables_.find(table);
  if (it == tables_.end())
    return {};
  auto &ts = it->second;

  // Check MemTable first
  char buf[PAGE_SIZE];
  int size;
  if (ts.memtable.Get(key, buf, &size)) {
    return {DeserializeRow(buf, size, ts.schema)};
  }
  if (ts.memtable.IsDeleted(key))
    return {}; // tombstone

  // Check SSTables (newest first)
  for (auto &sst : ts.sstables) {
    auto *entry = sst->Get(key);
    if (entry) {
      if (entry->deleted)
        return {};
      return {
          DeserializeRow(entry->data.data(), entry->data.size(), ts.schema)};
    }
  }
  return {};
}

std::vector<Row> LSMEngine::Scan(const std::string &table) {
  auto it = tables_.find(table);
  if (it == tables_.end())
    return {};
  auto &ts = it->second;

  // Merge all sources: collect latest version of each key
  std::map<int, std::pair<int, const char *>> latest; // key → (size, data)
  std::map<int, bool> deleted;
  std::map<int, std::vector<char>> owned_data; // keep data alive

  // MemTable entries (highest priority)
  ts.memtable.ForEach([&](int key, const MemEntry &e) {
    if (e.deleted) {
      deleted[key] = true;
    } else {
      owned_data[key] = e.data;
      latest[key] = {e.size, nullptr}; // mark as present
      deleted.erase(key);
    }
  });

  // SSTables (newest first)
  for (auto &sst : ts.sstables) {
    for (auto &e : sst->GetEntries()) {
      if (latest.count(e.key) || deleted.count(e.key))
        continue;
      if (e.deleted) {
        deleted[e.key] = true;
        continue;
      }
      owned_data[e.key].assign(e.data.begin(), e.data.end());
      latest[e.key] = {(int)e.data.size(), nullptr};
    }
  }

  // Deserialize results
  std::vector<Row> results;
  for (auto &[key, info] : latest) {
    if (deleted.count(key))
      continue;
    auto &data = owned_data[key];
    results.push_back(DeserializeRow(data.data(), data.size(), ts.schema));
  }
  return results;
}

std::vector<Row> LSMEngine::RangeScan(const std::string &table, int low,
                                      int high) {
  // Simple: scan all and filter by key range
  auto all = Scan(table);
  auto &ts = tables_[table];
  std::vector<Row> results;
  for (auto &row : all) {
    if (ts.schema.pk_index >= 0) {
      int pk = row[ts.schema.pk_index].int_val;
      if (pk >= low && pk <= high)
        results.push_back(row);
    }
  }
  return results;
}

bool LSMEngine::Remove(const std::string &table, int key) {
  auto it = tables_.find(table);
  if (it == tables_.end())
    return false;
  it->second.memtable.Delete(key);
  MaybeFlush(table);
  return true;
}

bool LSMEngine::Update(const std::string &table, int key, const Row &new_row) {
  auto it = tables_.find(table);
  if (it == tables_.end())
    return false;
  auto &ts = it->second;
  char buf[PAGE_SIZE];
  int size = SerializeRow(new_row, ts.schema, buf);
  ts.memtable.Put(key, buf, size);
  MaybeFlush(table);
  return true;
}

// ============================================================
// Flush MemTable → SSTable
// ============================================================

void LSMEngine::FlushMemTable(const std::string &table) {
  auto it = tables_.find(table);
  if (it == tables_.end())
    return;
  auto &ts = it->second;
  if (ts.memtable.Size() == 0)
    return;

  std::vector<SSEntry> entries;
  ts.memtable.ForEach([&](int key, const MemEntry &e) {
    SSEntry se;
    se.key = key;
    se.deleted = e.deleted;
    se.data = e.data;
    entries.push_back(se);
  });

  int sst_id = ts.next_sst_id++;
  std::string path = SSTablePath(table, sst_id);
  SSTable::Write(path, entries);

  // Insert at front (newest first)
  ts.sstables.insert(ts.sstables.begin(), std::make_unique<SSTable>(path));
  ts.memtable.Clear();

  std::cout << "[LSM] Flushed MemTable to " << path << " (" << entries.size()
            << " entries)\n";
}

void LSMEngine::MaybeFlush(const std::string &table) {
  if (tables_[table].memtable.IsFull()) {
    FlushMemTable(table);
  }
}

// ============================================================
// Size-Tiered Compaction — merge all SSTables into one
// ============================================================

void LSMEngine::Compact(const std::string &table) {
  auto it = tables_.find(table);
  if (it == tables_.end())
    return;
  auto &ts = it->second;
  if (ts.sstables.size() < 2)
    return;

  std::cout << "[LSM] Compacting " << ts.sstables.size() << " SSTables...\n";

  std::vector<SSTable *> ptrs;
  for (auto &s : ts.sstables)
    ptrs.push_back(s.get());

  int new_id = ts.next_sst_id++;
  std::string new_path = SSTablePath(table, new_id);
  SSTable::Merge(ptrs, new_path);

  // Remove old SSTable files
  for (auto &s : ts.sstables) {
    std::filesystem::remove(s->GetPath());
  }
  ts.sstables.clear();

  ts.sstables.push_back(std::make_unique<SSTable>(new_path));
  std::cout << "[LSM] Compaction done → " << new_path << " ("
            << ts.sstables[0]->Size() << " entries)\n";
}

int LSMEngine::GetMemTableSize(const std::string &table) const {
  auto it = tables_.find(table);
  return it != tables_.end() ? it->second.memtable.Size() : 0;
}

int LSMEngine::GetSSTableCount(const std::string &table) const {
  auto it = tables_.find(table);
  return it != tables_.end() ? (int)it->second.sstables.size() : 0;
}
