// ============================================================
// DoraDB — Main Entry Point (REPL + Tests)
//
// Usage:
//   ./doradb           → interactive REPL
//   ./doradb --test    → run integration tests
// ============================================================

#include "lsm/lsm_engine.h"
#include "parser/parser.h"
#include "parser/tokenizer.h"
#include "recovery/recovery.h"
#include "recovery/wal.h"
#include "storage/heap_engine.h"
#include "txn/lock_manager.h"
#include "txn/txn_manager.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

// ============================================================
// Execute a SQL script file (\i command)
// ============================================================
static void ExecuteFile(HeapEngine &engine, const std::string &filename) {
  std::ifstream file(filename);
  if (!file.is_open()) {
    std::cout << "Error: cannot open file '" << filename << "'\n";
    return;
  }
  std::string line;
  while (std::getline(file, line)) {
    if (line.empty() || line[0] == '-')
      continue; // skip empty/comments
    std::cout << "  > " << line << "\n";
    try {
      auto tokens = Tokenizer(line).Tokenize();
      auto stmt = Parser(tokens).Parse();
      std::string result = engine.Execute(stmt);
      std::cout << result << "\n";
    } catch (const std::exception &e) {
      std::cout << "Error: " << e.what() << "\n";
    }
  }
}

// ============================================================
// Execute one SQL line
// ============================================================
static void ExecuteLine(HeapEngine &engine, const std::string &line) {
  auto tokens = Tokenizer(line).Tokenize();
  auto stmt = Parser(tokens).Parse();
  std::string result = engine.Execute(stmt);
  std::cout << result << "\n";
}

// ============================================================
// Integration tests for M3
// ============================================================
static int tests_passed = 0, tests_total = 0;

void CHECK(bool cond, const std::string &msg) {
  tests_total++;
  std::cout << (cond ? "  [PASS] " : "  [FAIL] ") << msg << "\n";
  if (cond)
    tests_passed++;
}

static int RunTests() {
  std::cout << "========================================\n";
  std::cout << "  DoraDB — Milestone 3 Tests\n";
  std::cout << "  Query Execution + Optimizer\n";
  std::cout << "========================================\n";

  std::filesystem::remove_all("test_data");
  {
    HeapEngine engine("test_data");

    // CREATE TABLE
    std::cout << "\n=== CREATE TABLE ===\n";
    ExecuteLine(engine, "CREATE TABLE students (id INT, name VARCHAR(50), "
                        "active BOOL, PRIMARY KEY(id));");
    CHECK(engine.GetCatalog().GetTable("students") != nullptr, "table created");

    // INSERT
    std::cout << "\n=== INSERT ===\n";
    ExecuteLine(engine, "INSERT INTO students VALUES (1, 'Alice', true);");
    ExecuteLine(engine, "INSERT INTO students VALUES (2, 'Bob', false);");
    ExecuteLine(engine, "INSERT INTO students VALUES (3, 'Charlie', true);");
    ExecuteLine(engine, "INSERT INTO students VALUES (4, 'Diana', false);");
    ExecuteLine(engine, "INSERT INTO students VALUES (5, 'Eve', true);");
    CHECK(engine.GetStats("students").row_count == 5, "5 rows inserted");

    // SELECT *
    std::cout << "\n=== SELECT * ===\n";
    auto rows = engine.Scan("students");
    CHECK((int)rows.size() == 5, "scan returns 5 rows");

    // SELECT with WHERE (index scan)
    std::cout << "\n=== SELECT WHERE id = 3 (IndexScan) ===\n";
    ExecuteLine(engine, "SELECT * FROM students WHERE id = 3;");
    auto r = engine.Get("students", 3);
    CHECK(r.size() == 1 && r[0][1].str_val == "Charlie",
          "index lookup: Charlie");

    // SELECT with range (should use index)
    std::cout << "\n=== SELECT WHERE id > 2 AND id <= 4 ===\n";
    ExecuteLine(engine, "SELECT * FROM students WHERE id > 2 AND id <= 4;");

    // SELECT with non-PK filter (SeqScan + Filter)
    std::cout << "\n=== SELECT WHERE active = true (SeqScan) ===\n";
    ExecuteLine(engine, "SELECT * FROM students WHERE active = true;");

    // SELECT specific columns
    std::cout << "\n=== SELECT name FROM students ===\n";
    ExecuteLine(engine, "SELECT name FROM students WHERE id = 1;");

    // UPDATE
    std::cout << "\n=== UPDATE ===\n";
    ExecuteLine(engine, "UPDATE students SET name = 'Alicia' WHERE id = 1;");
    r = engine.Get("students", 1);
    CHECK(r.size() == 1 && r[0][1].str_val == "Alicia",
          "update: Alice → Alicia");

    // DELETE
    std::cout << "\n=== DELETE ===\n";
    ExecuteLine(engine, "DELETE FROM students WHERE id = 5;");
    CHECK(engine.Get("students", 5).empty(), "Eve deleted");
    CHECK(engine.GetStats("students").row_count == 4, "4 rows remain");

    // JOIN test
    std::cout << "\n=== JOIN ===\n";
    ExecuteLine(engine, "CREATE TABLE courses (cid INT, student_id INT, course "
                        "VARCHAR(30), PRIMARY KEY(cid));");
    ExecuteLine(engine, "INSERT INTO courses VALUES (1, 1, 'DBMS');");
    ExecuteLine(engine, "INSERT INTO courses VALUES (2, 2, 'OS');");
    ExecuteLine(engine, "INSERT INTO courses VALUES (3, 1, 'Networks');");
    ExecuteLine(engine, "INSERT INTO courses VALUES (4, 3, 'Algorithms');");

    ExecuteLine(engine, "SELECT * FROM students JOIN courses ON students.id = "
                        "courses.student_id;");

    // Verify join results
    // Alicia(1) should match DBMS and Networks, Bob(2) matches OS, Charlie(3)
    // matches Algorithms Diana(4) has no match

    std::cout << "\n=== Persistence ===\n";
  }

  // Persistence test: reopen engine
  {
    HeapEngine engine("test_data");
    auto r = engine.Get("students", 2);
    CHECK(r.size() == 1 && r[0][1].str_val == "Bob", "Bob survives restart");
    CHECK(engine.Get("students", 5).empty(), "Eve still deleted after restart");
    CHECK(engine.GetStats("students").row_count == 4,
          "row count correct after restart");
  }

  std::cout << "\n========================================\n";
  std::cout << "  Results: " << tests_passed << "/" << tests_total
            << " passed\n";
  std::cout << "========================================\n";

  std::filesystem::remove_all("test_data");

  // ============================================================
  // M4: Transaction + Locking Tests
  // ============================================================
  std::cout << "\n========================================\n";
  std::cout << "  DoraDB — Milestone 4 Tests\n";
  std::cout << "  Transactions + Locking\n";
  std::cout << "========================================\n";

  // --- Test: Basic locking ---
  std::cout << "\n=== Basic Locking ===\n";
  {
    LockManager lm;
    TxnManager tm(&lm);

    int t1 = tm.Begin();
    int t2 = tm.Begin();

    RID r1{1, 0}, r2{2, 0};

    // Both can get shared locks on same row
    CHECK(lm.LockShared(t1, r1), "T1 gets SHARED on r1");
    CHECK(lm.LockShared(t2, r1), "T2 gets SHARED on r1 (compatible)");

    // T1 gets exclusive on r2
    CHECK(lm.LockExclusive(t1, r2), "T1 gets EXCLUSIVE on r2");

    tm.Commit(t1);
    tm.Commit(t2);
    CHECK(tm.GetState(t1) == TxnState::COMMITTED, "T1 committed");
    CHECK(tm.GetState(t2) == TxnState::COMMITTED, "T2 committed");
  }

  // --- Test: Deadlock detection ---
  std::cout << "\n=== Deadlock Detection Demo ===\n";
  std::cout << "Scenario: T1 holds A, T2 holds B, T1 wants B, T2 wants A\n\n";
  {
    LockManager lm;
    TxnManager tm(&lm);
    RID rowA{10, 0}, rowB{20, 0};

    int t1 = tm.Begin();
    int t2 = tm.Begin();

    // T1 locks row A, T2 locks row B
    CHECK(lm.LockExclusive(t1, rowA), "T1 locks row A");
    CHECK(lm.LockExclusive(t2, rowB), "T2 locks row B");

    // Now T2 tries to lock row A → must wait (T1 holds it)
    // Then T1 tries to lock row B → deadlock!
    // We run T2's request in a thread so it blocks,
    // then T1's request detects the cycle.

    bool t2_got_a = false;
    std::thread thread2([&]() {
      t2_got_a = lm.LockExclusive(t2, rowA); // blocks waiting for T1
    });

    // Give thread2 time to block
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // T1 tries to lock row B → deadlock detected, T1 is victim
    bool t1_got_b = lm.LockExclusive(t1, rowB);

    if (!t1_got_b) {
      std::cout << "\n→ T1 was chosen as deadlock victim, aborting T1\n";
      tm.Abort(t1);
      // Now T2 should be able to proceed
    } else if (!t2_got_a) {
      std::cout << "\n→ T2 was chosen as deadlock victim, aborting T2\n";
      tm.Abort(t2);
    }

    thread2.join();

    bool one_aborted = (tm.GetState(t1) == TxnState::ABORTED ||
                        tm.GetState(t2) == TxnState::ABORTED);
    CHECK(one_aborted, "One transaction aborted (deadlock resolved)");

    bool one_can_proceed = t1_got_b || t2_got_a;
    CHECK(one_can_proceed, "Other transaction can proceed");

    // Commit the surviving txn
    if (tm.GetState(t1) == TxnState::ACTIVE)
      tm.Commit(t1);
    if (tm.GetState(t2) == TxnState::ACTIVE)
      tm.Commit(t2);
  }

  // --- Test: Strict 2PL (locks held until commit) ---
  std::cout << "\n=== Strict 2PL Demo ===\n";
  {
    LockManager lm;
    TxnManager tm(&lm);
    RID r1{30, 0};

    int t1 = tm.Begin();
    lm.LockExclusive(t1, r1);

    // T2 should not be able to get the lock while T1 is active
    int t2 = tm.Begin();
    bool t2_blocked = false;
    std::thread waiter([&]() {
      t2_blocked = true;
      lm.LockShared(t2, r1); // blocks until T1 commits
      t2_blocked = false;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(t2_blocked, "T2 blocked while T1 holds exclusive lock");

    // Commit T1 → releases locks → T2 unblocks
    tm.Commit(t1);
    waiter.join();

    CHECK(!t2_blocked, "T2 unblocked after T1 committed");
    CHECK(lm.GetLockInfo(t2, r1) == "SHARED", "T2 now holds SHARED lock");
    tm.Commit(t2);
  }

  std::cout << "\n========================================\n";
  std::cout << "  DoraDB — Milestone 5 Tests\n";
  std::cout << "  LSM Engine + WAL Recovery\n";
  std::cout << "========================================\n";

  // --- Test: LSM Engine ---
  std::cout << "\n=== LSM Engine (MemTable & Compaction) ===\n";
  std::filesystem::remove_all("test_lsm");
  {
    LSMEngine lsm("test_lsm");
    Schema s;
    s.columns.push_back({"id", DataType::INT});
    s.pk_index = 0;
    lsm.CreateTable("users", s);

    // Insert enough to force flush (assuming MEMTABLE_MAX_ENTRIES is small
    // enough or we force it) We'll just insert 5 and use ForceFlush
    for (int i = 1; i <= 5; i++) {
      lsm.Insert("users", {Value::Int(i)});
    }

    CHECK(lsm.GetMemTableSize("users") == 5, "MemTable has 5 entries");

    // Force flush to SSTable
    lsm.FlushMemTable("users");
    CHECK(lsm.GetMemTableSize("users") == 0, "MemTable empty after flush");
    CHECK(lsm.GetSSTableCount("users") == 1, "1 SSTable created");

    // Insert more and flush again
    for (int i = 6; i <= 10; i++) {
      lsm.Insert("users", {Value::Int(i)});
    }
    lsm.FlushMemTable("users");
    CHECK(lsm.GetSSTableCount("users") == 2, "2 SSTables created");

    // Scan should merge MemTable and all SSTables correctly
    auto rows = lsm.Scan("users");
    CHECK(rows.size() == 10, "Scan returns all 10 rows from multiple SSTables");

    // Update a row and delete another
    lsm.Update("users", 2, {Value::Int(200)}); // Update id 2
    lsm.Remove("users", 8);                    // Delete id 8

    auto r2 = lsm.Get("users", 2);
    CHECK(!r2.empty() && r2[0][0].int_val == 200,
          "Update applied correctly (tombstone/override)");
    CHECK(lsm.Get("users", 8).empty(), "Delete applied correctly (tombstone)");

    // Compact the SSTables
    lsm.Compact("users");
    CHECK(lsm.GetSSTableCount("users") == 1, "SSTables compacted into 1");

    // Scan again after compaction
    rows = lsm.Scan("users");
    CHECK(rows.size() == 9, "Scan returns 9 rows after compaction (1 deleted)");
  }
  std::filesystem::remove_all("test_lsm");

  // --- Test: WAL and Recovery ---
  std::cout << "\n=== WAL & ARIES Recovery ===\n";
  std::filesystem::remove_all("test_wal");
  std::filesystem::create_directories("test_wal");
  {
    WAL wal("test_wal/log.bin");
    // Simulate T1 (Commits)
    wal.AppendBegin(1);
    wal.AppendInsert(1, "users", {10, 0}, "A", 1);
    wal.AppendCommit(1);

    // Simulate T2 (Active/Crashes)
    wal.AppendBegin(2);
    wal.AppendInsert(2, "users", {11, 0}, "B", 1);

    // Simulate T3 (Aborts)
    wal.AppendBegin(3);
    wal.AppendInsert(3, "users", {12, 0}, "C", 1);
    wal.AppendAbort(3);
  }

  // Recover
  {
    WAL wal("test_wal/log.bin");
    std::unordered_map<std::string, RecoveryManager::TableAccess> dummy_tables;
    // In a real recovery we pass actual heap pointers. Here we just test the
    // analysis logic.
    auto res = RecoveryManager::Recover(wal, dummy_tables);

    CHECK(res.committed_txns == 1, "Recovery found 1 committed txn (T1)");
    CHECK(res.aborted_txns == 1, "Recovery found 1 txn to undo (T2 crashed)");
  }
  std::filesystem::remove_all("test_wal");

  std::cout << "\n========================================\n";
  std::cout << "  Results: " << tests_passed << "/" << tests_total
            << " passed\n";
  std::cout << "========================================\n";

  return (tests_passed == tests_total) ? 0 : 1;
}

// ============================================================
// Benchmark: HeapEngine vs LSMEngine
// ============================================================
static void RunBenchmark() {
  std::cout << "========================================\n";
  std::cout << "  DoraDB Benchmark: HeapEngine vs LSM\n";
  std::cout << "========================================\n";

  const int NUM_OPS = 50000;
  Schema schema;
  schema.columns.push_back({"id", DataType::INT});
  schema.pk_index = 0;

  std::filesystem::remove_all("bench_heap");
  std::filesystem::remove_all("bench_lsm");

  // HeapEngine Benchmark
  {
    HeapEngine heap("bench_heap");
    heap.CreateTable("data", schema);

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < NUM_OPS; i++) {
      heap.Insert("data", {Value::Int(i)});
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
                  .count();
    std::cout << "[HeapEngine] " << NUM_OPS << " Inserts: " << ms << " ms ("
              << (NUM_OPS * 1000.0 / ms) << " ops/sec)\n";
  }

  // LSMEngine Benchmark
  {
    LSMEngine lsm("bench_lsm");
    lsm.CreateTable("data", schema);

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < NUM_OPS; i++) {
      lsm.Insert("data", {Value::Int(i)});
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start)
                  .count();
    std::cout << "[LSMEngine]  " << NUM_OPS << " Inserts: " << ms << " ms ("
              << (NUM_OPS * 1000.0 / ms) << " ops/sec)\n";
  }

  std::filesystem::remove_all("bench_heap");
  std::filesystem::remove_all("bench_lsm");
  std::cout << "========================================\n";
}

// ============================================================
// REPL
// ============================================================
static void RunREPL() {
  std::cout << "DoraDB v0.5 — A MiniDB Engine\n";
  std::cout
      << "Type SQL statements, \\i <file> to run script, \\q to quit.\n\n";

  HeapEngine engine("data");

  while (true) {
    std::cout << "DoraDB> " << std::flush;
    std::string line;
    if (!std::getline(std::cin, line))
      break;

    // Trim
    while (!line.empty() && std::isspace(line.front()))
      line.erase(line.begin());
    while (!line.empty() && std::isspace(line.back()))
      line.pop_back();
    if (line.empty())
      continue;

    // Meta-commands
    if (line == "\\q") {
      std::cout << "Bye!\n";
      break;
    }
    if (line.starts_with("\\i ")) {
      ExecuteFile(engine, line.substr(3));
      continue;
    }
    if (line == "\\dt") {
      for (auto &name : engine.GetCatalog().GetAllTableNames()) {
        auto *info = engine.GetCatalog().GetTable(name);
        std::cout << "  " << name << " (" << info->schema.columns.size()
                  << " columns, " << engine.GetStats(name).row_count
                  << " rows)\n";
      }
      continue;
    }

    try {
      ExecuteLine(engine, line);
    } catch (const std::exception &e) {
      std::cout << "Error: " << e.what() << "\n";
    }
  }
}

// ============================================================
// Main
// ============================================================
int main(int argc, char *argv[]) {
  if (argc > 1) {
    std::string arg = argv[1];
    if (arg == "--test")
      return RunTests();
    if (arg == "--bench") {
      RunBenchmark();
      return 0;
    }
  }
  RunREPL();
  return 0;
}
