#pragma once

#include "common/types.h"
#include <optional>
#include <string>
#include <unordered_map>

// ============================================================
// Catalog — keeps track of all tables, their schemas, heap file
// locations, and index file paths.
//
// Persists to a simple text file so schemas survive restart.
// ============================================================

struct TableInfo {
  std::string name;
  Schema schema;
  int first_page_id;      // first heap file page for this table
  std::string index_file; // path to B+Tree index file (empty if no PK)
};

class Catalog {
public:
  explicit Catalog(const std::string &catalog_file);

  // Create a new table entry. Returns false if table already exists.
  bool CreateTable(const std::string &name, const Schema &schema,
                   int first_page_id, const std::string &index_file = "");

  // Look up a table. Returns nullptr if not found.
  const TableInfo *GetTable(const std::string &name) const;

  // Get all table names
  std::vector<std::string> GetAllTableNames() const;

  // Persist catalog to disk
  void Save();

  // Load catalog from disk
  void Load();

private:
  std::string catalog_file_;
  std::unordered_map<std::string, TableInfo> tables_;
};
