#include "storage/heap_engine.h"
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

HeapEngine::HeapEngine(const std::string &data_dir)
    : data_dir_(data_dir), catalog_(data_dir + "/catalog.txt") {
  std::filesystem::create_directories(data_dir);
  disk_mgr_ = new DiskManager(data_dir + "/heap.db");
  buffer_pool_ = new BufferPool(disk_mgr_, BUFFER_POOL_SIZE);
  LoadTables();
}

HeapEngine::~HeapEngine() {
  for (auto &[_, ts] : tables_) {
    delete ts.index;
    delete ts.heap;
  }
  buffer_pool_->FlushAll();
  delete buffer_pool_;
  delete disk_mgr_;
}

void HeapEngine::LoadTables() {
  for (auto &name : catalog_.GetAllTableNames()) {
    auto *info = catalog_.GetTable(name);
    if (!info)
      continue;
    TableState ts;
    ts.heap = new HeapFile(buffer_pool_, info->first_page_id);
    if (!info->index_file.empty()) {
      ts.index = new BPlusTree(info->index_file);
    }
    // Count rows for stats
    ts.heap->Scan([&](const RID &, const char *data, int size) {
      ts.stats.row_count++;
      Row row = DeserializeRow(data, size, info->schema);
      if (info->schema.pk_index >= 0) {
        int pk = row[info->schema.pk_index].int_val;
        if (ts.stats.row_count == 1) {
          ts.stats.pk_min = pk;
          ts.stats.pk_max = pk;
        } else {
          ts.stats.pk_min = std::min(ts.stats.pk_min, pk);
          ts.stats.pk_max = std::max(ts.stats.pk_max, pk);
        }
      }
    });
    tables_[name] = ts;
  }
}

// ---- StorageEngine interface ----

void HeapEngine::CreateTable(const std::string &name, const Schema &schema) {
  if (tables_.count(name))
    throw std::runtime_error("Table already exists: " + name);
  TableState ts;
  ts.heap = new HeapFile(buffer_pool_);
  int first_page = ts.heap->Create();
  std::string idx_file;
  if (schema.pk_index >= 0) {
    idx_file = data_dir_ + "/" + name + "_pk.idx";
    ts.index = new BPlusTree(idx_file);
  }
  catalog_.CreateTable(name, schema, first_page, idx_file);
  tables_[name] = ts;
}

bool HeapEngine::Insert(const std::string &table, const Row &row) {
  auto it = tables_.find(table);
  if (it == tables_.end())
    return false;
  auto &ts = it->second;
  const Schema &schema = GetSchema(table);

  char buf[PAGE_SIZE];
  int size = SerializeRow(row, schema, buf);
  RID rid = ts.heap->InsertRow(buf, size);

  // Update B+Tree index
  if (ts.index && schema.pk_index >= 0) {
    int pk = row[schema.pk_index].int_val;
    ts.index->Insert(pk, rid);
    ts.stats.row_count++;
    ts.stats.pk_min = std::min(ts.stats.pk_min, pk);
    ts.stats.pk_max = std::max(ts.stats.pk_max, pk);
  } else {
    ts.stats.row_count++;
  }
  return true;
}

std::vector<Row> HeapEngine::Scan(const std::string &table) {
  auto it = tables_.find(table);
  if (it == tables_.end())
    return {};
  const Schema &schema = GetSchema(table);
  std::vector<Row> results;
  it->second.heap->Scan([&](const RID &, const char *data, int size) {
    results.push_back(DeserializeRow(data, size, schema));
  });
  return results;
}

std::vector<Row> HeapEngine::Get(const std::string &table, int key) {
  auto it = tables_.find(table);
  if (it == tables_.end())
    return {};
  const Schema &schema = GetSchema(table);
  if (it->second.index) {
    auto rid = it->second.index->Search(key);
    if (rid) {
      char buf[PAGE_SIZE];
      int size;
      if (it->second.heap->GetRow(*rid, buf, &size))
        return {DeserializeRow(buf, size, schema)};
    }
    return {};
  }
  // Fallback: scan
  std::vector<Row> results;
  it->second.heap->Scan([&](const RID &, const char *data, int size) {
    Row r = DeserializeRow(data, size, schema);
    if (schema.pk_index >= 0 && r[schema.pk_index].int_val == key)
      results.push_back(r);
  });
  return results;
}

std::vector<Row> HeapEngine::RangeScan(const std::string &table, int low,
                                       int high) {
  auto it = tables_.find(table);
  if (it == tables_.end())
    return {};
  const Schema &schema = GetSchema(table);
  if (it->second.index) {
    auto rids = it->second.index->RangeScan(low, high);
    std::vector<Row> results;
    char buf[PAGE_SIZE];
    int size;
    for (auto &rid : rids) {
      if (it->second.heap->GetRow(rid, buf, &size))
        results.push_back(DeserializeRow(buf, size, schema));
    }
    return results;
  }
  // Fallback: scan and filter
  std::vector<Row> results;
  it->second.heap->Scan([&](const RID &, const char *data, int size) {
    Row r = DeserializeRow(data, size, schema);
    if (schema.pk_index >= 0) {
      int pk = r[schema.pk_index].int_val;
      if (pk >= low && pk <= high)
        results.push_back(r);
    }
  });
  return results;
}

bool HeapEngine::Remove(const std::string &table, int key) {
  auto it = tables_.find(table);
  if (it == tables_.end())
    return false;
  if (it->second.index) {
    auto rid = it->second.index->Search(key);
    if (rid) {
      it->second.heap->DeleteRow(*rid);
      it->second.index->Delete(key);
      it->second.stats.row_count--;
      return true;
    }
  }
  return false;
}

bool HeapEngine::Update(const std::string &table, int key, const Row &new_row) {
  auto it = tables_.find(table);
  if (it == tables_.end())
    return false;
  const Schema &schema = GetSchema(table);
  if (it->second.index) {
    auto rid = it->second.index->Search(key);
    if (rid) {
      char buf[PAGE_SIZE];
      int size = SerializeRow(new_row, schema, buf);
      RID new_rid = it->second.heap->UpdateRow(*rid, buf, size);
      if (!(new_rid == *rid)) {
        it->second.index->Delete(key);
        int new_pk = new_row[schema.pk_index].int_val;
        it->second.index->Insert(new_pk, new_rid);
      }
      return true;
    }
  }
  return false;
}

// ---- Accessors ----

HeapFile *HeapEngine::GetHeapFile(const std::string &table) {
  return tables_.at(table).heap;
}
BPlusTree *HeapEngine::GetIndex(const std::string &table) {
  return tables_.at(table).index;
}

const Schema &HeapEngine::GetSchema(const std::string &table) const {
  return catalog_.GetTable(table)->schema;
}

const TableStats &HeapEngine::GetStats(const std::string &table) const {
  return tables_.at(table).stats;
}

// ---- SQL Execution ----

std::string HeapEngine::Execute(const Statement &stmt) {
  switch (stmt.type) {
  case StmtType::CREATE_TABLE:
    return ExecCreateTable(stmt.create_table);
  case StmtType::INSERT:
    return ExecInsert(stmt.insert);
  case StmtType::SELECT:
    return ExecSelect(stmt.select);
  case StmtType::DELETE_STMT:
    return ExecDelete(stmt.delete_stmt);
  case StmtType::UPDATE:
    return ExecUpdate(stmt.update);
  }
  return "Unknown statement type";
}

std::string HeapEngine::ExecCreateTable(const CreateTableStmt &stmt) {
  Schema schema;
  schema.columns = stmt.columns;
  schema.pk_index = stmt.pk_index;
  CreateTable(stmt.table_name, schema);
  return "Table '" + stmt.table_name + "' created.";
}

std::string HeapEngine::ExecInsert(const InsertStmt &stmt) {
  if (tables_.find(stmt.table_name) == tables_.end()) {
    return "Error: Table '" + stmt.table_name + "' does not exist.";
  }
  Insert(stmt.table_name, stmt.values);
  return "Inserted 1 row.";
}

std::string HeapEngine::ExecSelect(const SelectStmt &stmt) {
  if (tables_.find(stmt.table_name) == tables_.end()) {
    return "Error: Table '" + stmt.table_name + "' does not exist.";
  }
  if (stmt.join.has_value() &&
      tables_.find(stmt.join->table_name) == tables_.end()) {
    return "Error: Join table '" + stmt.join->table_name + "' does not exist.";
  }
  auto plan = CreateSelectPlan(stmt, this);
  plan->Open();

  // Determine output schema
  Schema out_schema;
  const Schema &base_schema = GetSchema(stmt.table_name);
  out_schema.columns = base_schema.columns;
  if (stmt.join.has_value()) {
    const Schema &rs = GetSchema(stmt.join->table_name);
    out_schema.columns.insert(out_schema.columns.end(), rs.columns.begin(),
                              rs.columns.end());
  }

  // If projecting specific columns, build projected schema
  if (!stmt.select_all && !stmt.columns.empty()) {
    Schema proj_schema;
    for (auto &ref : stmt.columns) {
      int idx = out_schema.FindColumn(ref.column);
      if (idx >= 0)
        proj_schema.columns.push_back(out_schema.columns[idx]);
    }
    out_schema = proj_schema;
  }

  std::vector<Row> results;
  OutputRow out;
  while (plan->Next(out)) {
    results.push_back(out.values);
  }
  plan->Close();

  return FormatResults(results, out_schema);
}

std::string HeapEngine::ExecDelete(const DeleteStmt &stmt) {
  if (tables_.find(stmt.table_name) == tables_.end()) {
    return "Error: Table '" + stmt.table_name + "' does not exist.";
  }
  const Schema &schema = GetSchema(stmt.table_name);
  HeapFile *heap = GetHeapFile(stmt.table_name);
  BPlusTree *index = GetIndex(stmt.table_name);

  // Use optimizer for scan choice
  SelectStmt scan_stmt;
  scan_stmt.select_all = true;
  scan_stmt.table_name = stmt.table_name;
  scan_stmt.where_clause = stmt.where_clause;

  auto plan = CreateSelectPlan(scan_stmt, this);
  plan->Open();

  std::vector<std::pair<RID, int>> to_delete; // rid + pk value
  OutputRow out;
  while (plan->Next(out)) {
    int pk = (schema.pk_index >= 0) ? out.values[schema.pk_index].int_val : 0;
    to_delete.push_back({out.rid, pk});
  }
  plan->Close();

  for (auto &[rid, pk] : to_delete) {
    heap->DeleteRow(rid);
    if (index)
      index->Delete(pk);
    tables_[stmt.table_name].stats.row_count--;
  }

  return "Deleted " + std::to_string(to_delete.size()) + " row(s).";
}

std::string HeapEngine::ExecUpdate(const UpdateStmt &stmt) {
  if (tables_.find(stmt.table_name) == tables_.end()) {
    return "Error: Table '" + stmt.table_name + "' does not exist.";
  }
  const Schema &schema = GetSchema(stmt.table_name);
  HeapFile *heap = GetHeapFile(stmt.table_name);
  BPlusTree *index = GetIndex(stmt.table_name);

  SelectStmt scan_stmt;
  scan_stmt.select_all = true;
  scan_stmt.table_name = stmt.table_name;
  scan_stmt.where_clause = stmt.where_clause;

  auto plan = CreateSelectPlan(scan_stmt, this);
  plan->Open();

  int count = 0;
  OutputRow out;
  std::vector<std::pair<RID, Row>> to_update;
  while (plan->Next(out)) {
    Row new_row = out.values;
    for (auto &[col_name, val] : stmt.assignments) {
      int idx = schema.FindColumn(col_name);
      if (idx >= 0)
        new_row[idx] = val;
    }
    to_update.push_back({out.rid, new_row});
  }
  plan->Close();

  for (auto &[rid, new_row] : to_update) {
    char buf[PAGE_SIZE];
    int size = SerializeRow(new_row, schema, buf);
    RID new_rid = heap->UpdateRow(rid, buf, size);

    if (index && !(new_rid == rid)) {
      // Row moved — update index
      int old_pk = 0, new_pk = new_row[schema.pk_index].int_val;
      // Get old PK from original row
      char old_buf[PAGE_SIZE];
      int old_size;
      // We already deleted the old row in UpdateRow, use the new pk
      index->Delete(old_pk);
      index->Insert(new_pk, new_rid);
    }
    count++;
  }

  return "Updated " + std::to_string(count) + " row(s).";
}

// ---- Result formatting ----

std::string HeapEngine::FormatResults(const std::vector<Row> &rows,
                                      const Schema &schema) const {
  if (rows.empty() && schema.columns.empty())
    return "(0 rows)";

  // Calculate column widths
  std::vector<int> widths;
  for (auto &col : schema.columns) {
    widths.push_back(std::max((int)col.name.size(), 4));
  }
  for (auto &row : rows) {
    for (int i = 0; i < (int)row.size() && i < (int)widths.size(); i++) {
      widths[i] = std::max(widths[i], (int)row[i].ToString().size());
    }
  }

  std::ostringstream ss;

  // Header
  ss << "| ";
  for (int i = 0; i < (int)schema.columns.size(); i++) {
    ss << std::left << std::setw(widths[i]) << schema.columns[i].name << " | ";
  }
  ss << "\n|";
  for (int i = 0; i < (int)widths.size(); i++) {
    ss << std::string(widths[i] + 2, '-') << "|";
  }
  ss << "\n";

  // Rows
  for (auto &row : rows) {
    ss << "| ";
    for (int i = 0; i < (int)row.size() && i < (int)widths.size(); i++) {
      ss << std::left << std::setw(widths[i]) << row[i].ToString() << " | ";
    }
    ss << "\n";
  }
  ss << "(" << rows.size() << " row" << (rows.size() != 1 ? "s" : "") << ")";
  return ss.str();
}
