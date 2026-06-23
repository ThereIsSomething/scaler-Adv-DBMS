# DoraDB 

DoraDB is a mini SQL relational database engine built from scratch in C++20 for an Advanced DBMS capstone project. 
It supports standard SQL querying via an interactive REPL, fully features two backend storage engines (a traditional B+Tree backed Heap Engine and a Log-Structured Merge-Tree Engine), handles strict 2PL transaction concurrency, and features ARIES-style crash recovery.

## Project Architecture

DoraDB is highly modular and broken down into five distinct implementation milestones:

### M1: Storage Engine & Buffer Pool
- **DiskManager**: Manages raw POSIX file I/O operations.
- **Page Layout**: Implements a slotted page design, storing records with a header directory growing inward.
- **Buffer Pool**: A memory cache utilizing the **Clock Sweep** eviction policy, managing up to 64 pinned frames.
- **Heap File**: Linked list of slotted data pages for standard table row storage.

### M2: Indexing & SQL Parsing
- **B+Tree Index**: A fully on-disk, persistent B+Tree mapping keys to Row IDs (`page_id`, `slot_id`). Handles node splitting, leaf chaining for range scans, and lazy deletion.
- **Catalog**: Stores table schemas and metadata persistently in `catalog.txt`.
- **Query Parser**: A hand-written tokenizer and recursive-descent parser producing ASTs for `CREATE`, `INSERT`, `SELECT`, `DELETE`, and `UPDATE`.

### M3: Query Execution & Optimizer
- **Volcano Executor**: Iterator model plan nodes: `SeqScan`, `IndexScan`, `Filter`, `Projection`, and `NestedLoopJoin` (equi-joins).
- **Cost-Based Optimizer**: Analyzes `WHERE` clauses to evaluate selectivity and makes cost-based decisions between `SeqScan` vs `IndexScan`. Automatically selects the optimal outer table for Nested Loop Joins.

### M4: Transactions & Concurrency Control
- **Lock Manager**: Supports `SHARED` and `EXCLUSIVE` row-level locks. Implements a wait-for graph with Depth-First Search (DFS) for cycle detection to resolve deadlocks.
- **Transaction Manager**: Enforces **Strict 2PL** (Two-Phase Locking) by holding all acquired locks until `COMMIT` or `ABORT`. Supports rollbacks utilizing before-image write records.

### M5: Recovery & LSM-Tree Engine
- **WAL & ARIES Recovery**: Append-only Write-Ahead Log capturing operations. A recovery manager performs Analysis, Redo (for committed), and Undo (for active/aborted) phases upon startup.
- **LSM Engine**: A secondary storage engine implementing a Log-Structured Merge-Tree. 
  - **MemTable**: In-memory sorted store backed by `std::map` (Red-Black Tree).
  - **SSTable**: Immutable on-disk sorted string tables with binary-search lookups.
  - **Compaction**: Size-tiered background compaction.

## Building and Running

### Requirements
- CMake 3.16+
- C++20 compliant compiler (GCC/Clang)
- Linux/Unix Environment

### Build Instructions

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

### Usage

**1. Interactive REPL**
Launch the REPL by running the executable without arguments:
```bash
./doradb
```
Features supported:
- SQL Commands: `CREATE TABLE`, `INSERT`, `SELECT` (with `JOIN` and `WHERE`), `UPDATE`, `DELETE`.
- `\dt`: List all tables and row counts.
- `\i script.sql`: Execute a file of SQL commands.
- `\q`: Quit the REPL.

**2. Integration Tests**
Run the full 33-test suite covering M1-M5 milestones:
```bash
./doradb --test
```

**3. LSM vs Heap Benchmark**
Run the insert throughput benchmark comparing the Heap Engine to the LSM Engine:
```bash
./doradb --bench
```

### Benchmark Results
The LSM-Tree architecture achieves vastly superior write throughput due to sequential I/O and batching flushes, avoiding the constant page evictions and B+Tree node splits seen in the Heap Engine:

```text
========================================
  DoraDB Benchmark: HeapEngine vs LSM
========================================
[HeapEngine] 50000 Inserts: 2088 ms (23,946 ops/sec)
[LSMEngine]  50000 Inserts: 63 ms (793,651 ops/sec)
========================================
```
