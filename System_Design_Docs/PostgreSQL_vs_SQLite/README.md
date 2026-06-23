**Name:** Nitish Kumar Bhambu  
**Roll No:** 24BCS10589

---
## already done in lab03 as well

# PostgreSQL vs SQLite — Architecture Comparison

> Maine ye doc tab design kiya jab main in dono databases ke engine systems ko closely analyze kar raha tha. Kaise alag-alag design goals inke internal code aur performance characteristics ko change karte hain, wahi isme cover kiya hai — using clean diagrams, benchmark tests, aur conceptual breakdowns.

---

## 1. Problem Background

### Kyun banaye gaye ye dono database systems?

1990s ke phase mein database solutions matlab heavy enterprise applications hote the — Oracle ya DB2 types. Inko chalane ke liye dedicated server setups aur complex administrator skills chahiye hote the, jo college students ya individual developers ke reach se bahar tha.

**PostgreSQL** UC Berkeley ka ek advanced research project tha (POSTGRES name se, started by Michael Stonebraker in 1986). Iska ultimate goal tha ek high-utility extensible open-source RDBMS build karna jo complex query workloads ko scale pe support kar sake. Aaj ye tech startups se lekar large companies ke operations ka standard backend hai.

**SQLite** ki story thodi unique hai. D. Richard Hipp ne isko 2000 mein code kiya tha — US Navy damage system project ke constraints ke requirements mein. Unhe ek aisa datastore design chahiye tha jisme zero installation cost ho, pure features bina servers ke execute hon, aur complete database structure ek single file system block mein wrap ho sake (jaise offline navy ships/battleships pe data queries execute karna).

Summary:
- **PostgreSQL** = "Multiple clients concurrent access ke liye scale-ready RDBMS system."
- **SQLite** = "Zero setup, lightweight engine jo direct application runtime ke andar execute ho sake."

Inhi design targets se inke individual execution layers define hue hain.

---

## 2. Architecture Overview

### Internal Block Diagrams:

```
┌─────────────────────────────────────────────────────────────┐
│                    PostgreSQL Architecture                    │
│                                                               │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐                  │
│  │ Client 1 │  │ Client 2 │  │ Client 3 │   ... N clients  │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘                  │
│       │              │              │                         │
│       └──────────────┼──────────────┘                        │
│                      │                                        │
│              ┌───────▼────────┐                               │
│              │   Postmaster   │  (Main Process)               │
│              │   (Listener)   │                               │
│              └───────┬────────┘                               │
│                      │  fork()                                │
│         ┌────────────┼────────────┐                          │
│         │            │            │                           │
│   ┌─────▼─────┐┌────▼─────┐┌────▼─────┐                    │
│   │ Backend 1 ││ Backend 2 ││ Backend 3 │                    │
│   │ (Process)  ││ (Process) ││ (Process) │                   │
│   └─────┬─────┘└────┬─────┘└────┬─────┘                    │
│         │            │            │                           │
│         └────────────┼────────────┘                          │
│                      │                                        │
│              ┌───────▼────────┐                               │
│              │ Shared Memory  │                               │
│              │ ┌────────────┐ │                               │
│              │ │Shared Buffs│ │                               │
│              │ │  WAL Buffs │ │                               │
│              │ │  Lock Table│ │                               │
│              │ └────────────┘ │                               │
│              └───────┬────────┘                               │
│                      │                                        │
│              ┌───────▼────────┐                               │
│              │   Disk Storage │                               │
│              │  (Data files,  │                               │
│              │   WAL files)   │                               │
│              └────────────────┘                               │
└─────────────────────────────────────────────────────────────┘
```

SQLite processes mapping:

```
┌─────────────────────────────────────────┐
│          SQLite Architecture             │
│                                          │
│  ┌──────────────────────────┐           │
│  │     Your Application     │           │
│  │                          │           │
│  │  ┌────────────────────┐  │           │
│  │  │   SQLite Library   │  │           │
│  │  │                    │  │           │
│  │  │  ┌──────────────┐  │  │           │
│  │  │  │ SQL Compiler  │  │  │           │
│  │  │  │  (Parser +    │  │  │           │
│  │  │  │   Planner)    │  │  │           │
│  │  │  └──────┬───────┘  │  │           │
│  │  │         │          │  │           │
│  │  │  ┌──────▼───────┐  │  │           │
│  │  │  │Virtual Machine│  │  │           │
│  │  │  │  (VDBE)      │  │  │           │
│  │  │  └──────┬───────┘  │  │           │
│  │  │         │          │  │           │
│  │  │  ┌──────▼───────┐  │  │           │
│  │  │  │  B-Tree +    │  │  │           │
│  │  │  │   Pager      │  │  │           │
│  │  │  └──────┬───────┘  │  │           │
│  │  │         │          │  │           │
│  │  │  ┌──────▼───────┐  │  │           │
│  │  │  │    OS Layer   │  │  │           │
│  │  │  │  (VFS)       │  │  │           │
│  │  │  └──────┬───────┘  │  │           │
│  │  └─────────┼──────────┘  │           │
│  └────────────┼─────────────┘           │
│               │                          │
│        ┌──────▼───────┐                  │
│        │  Single DB   │                  │
│        │    File      │                  │
│        │ (.sqlite/.db)│                  │
│        └──────────────┘                  │
└─────────────────────────────────────────┘
```

### Process models

**PostgreSQL** operates on a **multi-process architecture**:
- A listener process (**Postmaster**) listens on port 5432.
- Each client connection triggers a `fork()` to create a dedicated **backend process**.
- Shared memory pools and background systems coordinate activities (e.g., `bgwriter`, `walwriter`, `autovacuum`, `checkpointer`, `stats collector`).

This multi-process design isolates clients; a crash in one backend process does not affect other active connections.

**SQLite** is an **in-process library**. There are no background daemons, network handlers, or isolated worker processes. SQLite runs directly within the application's memory space, reading and writing to a single database file.

---

## 3. Internal Design

### 3.1 Storage Engine Architecture

#### PostgreSQL storage layout

Data directories contain database heap tables, segment files, and status details:

```
$PGDATA/
├── base/                    ← Database storage
│   ├── 12345/               ← Database OID folder
│   │   ├── 16384            ← Table heap file segment
│   │   ├── 16384_fsm        ← Free Space Map
│   │   ├── 16384_vm         ← Visibility Map
│   │   └── ...
├── global/                  ← System catalogs shared space
├── pg_wal/                  ← WAL log blocks
└── pg_xact/                 ← Transaction commit status records
```

Postgres splits tables into 1GB segment files. Each segment consists of 8KB page blocks structured as follows:

```
┌─────────────────────────────────────────────┐
│                Page Header                   │
│  (pd_lsn, pd_checksum, pd_lower, pd_upper)   │
├─────────────────────────────────────────────┐
│          Item Pointers (Line Pointers)       │
│  [Item1] [Item2] ...                        │
│         ↓ Grows downward                     │
├─────────────────────────────────────────────┤
│             Free Space                       │
├─────────────────────────────────────────────┤
│         ↑ Grows upward                       │
│  [Tuple 3 data] [Tuple 2 data] [Tuple 1]   │
│           Actual Row Data                    │
├─────────────────────────────────────────────┤
│          Special Space                       │
└─────────────────────────────────────────────┘
```

Item pointers grow downward from the top of the free space, while row data tuples grow upward from the bottom.

#### SQLite storage layout

SQLite stores all tables, indexes, and schemas in a **single database file** divided into pages (default 4KB).

```
┌──────────────────────────────────────────┐
│  Page 1: Database Header + Schema Table  │
│  (First 100 bytes = File Header)         │
├──────────────────────────────────────────┤
│  Page 2: Schema Continuation             │
├──────────────────────────────────────────┤
│  Page 3: B-Tree Node (Table Data)        │
├──────────────────────────────────────────┤
│  ...                                     │
└──────────────────────────────────────────┘
```

SQLite tables are stored as B+ trees (where data lives only in the leaf nodes) keyed by a 64-bit signed integer called `rowid`. If a table doesn't have an explicit integer primary key, SQLite generates a hidden `rowid` column.

### 3.2 Index Implementation

**PostgreSQL** supports several index types:
- **B-Tree**: Default for equality and range queries.
- **Hash**: Optimized for simple equality checks.
- **GiST / SP-GiST**: Used for geometric, spatial, and range data.
- **GIN**: Designed for composite values like arrays and JSONB documents.
- **BRIN**: Block range indexes for large, ordered datasets.

**SQLite** focuses on a single index structure: the B-tree. It uses B+ trees for tables and standard B-trees (storing keys and row IDs) for indexes.

### 3.3 Concurrency and Transaction Management

#### PostgreSQL MVCC

PostgreSQL uses Multi-Version Concurrency Control (MVCC) to allow multiple versions of a row to exist at the same time.

```
BEFORE UPDATE:
┌──────────────────────────────────┐
│ Tuple Version 1                  │
│ xmin = 100, xmax = 0             │
│ data = "Nitish, Delhi"           │
└──────────────────────────────────┘

T200 updates row:
┌──────────────────────────────────┐
│ Tuple Version 1 (OLD)           │
│ xmin = 100, xmax = 200 (DEAD)    │
│ data = "Nitish, Delhi"           │
│ t_ctid → Version 2               │
└──────────────────────────────────┘
                 │
                 ▼
┌──────────────────────────────────┐
│ Tuple Version 2 (NEW)           │
│ xmin = 200, xmax = 0             │
│ data = "Nitish, Mumbai"          │
└──────────────────────────────────┘
```

Because updates append new versions rather than overwriting in place, readers and writers do not block each other. However, this generates dead tuples that must be cleaned up by the `VACUUM` process to prevent table bloat.

#### SQLite Concurrency

SQLite uses database-level locking to coordinate access:

```
Lock levels:
[UNLOCKED] → [SHARED] (Readers) → [RESERVED] (Single writer preparing) 
  → [PENDING] (Waiting for readers to exit) → [EXCLUSIVE] (Write block active)
```

By default, SQLite supports either multiple concurrent readers or a single writer. When configured in Write-Ahead Log (WAL) mode, SQLite allows concurrent reads and writes, though write operations remain serialized.

### 3.4 Durability Mechanisms

- **PostgreSQL WAL**: Changes are written to the Write-Ahead Log (`pg_wal/`) before being applied to data pages. Checkpoints flush dirty pages to disk to bound recovery times.
- **SQLite Journaling**:
  - **Rollback Journal (Default)**: Writes the original database pages to a journal file before modifying them. In case of a crash, the journal is replayed to restore the database to its original state.
  - **WAL Mode**: Appends new pages to a WAL file. A checkpoint process later merges these changes back into the main database file.

---

## 4. Design Trade-offs

### Client-Server vs Embedded

| Dimension | PostgreSQL | SQLite |
|---|---|---|
| Deployment | Server installation and configuration required | Simple library linkage, zero setup |
| Concurrency | Supports hundreds of concurrent active connections | One writer at a time |
| Network Access | Native TCP client-server connections | Local file system access only |
| Footprint | ~100MB disk footprint | Small footprint (~600KB library) |
| Administration | Requires DBA tasks (vacuuming, monitoring) | Zero maintenance |

### Storage Layouts

PostgreSQL uses unordered heap tables, which requires index searches to perform a secondary lookups in the heap. SQLite tables are structured as B+ trees, meaning primary key lookups directly retrieve the target row data in a single step.

---

## 5. Experiments / Observations

### Experiment 1: Database File Size comparison

Inserting 10,000 records into a test table:

```sql
CREATE TABLE students (
    id INTEGER PRIMARY KEY,
    name TEXT,
    age INTEGER,
    city TEXT
);

INSERT INTO students VALUES (1, 'Nitish', 21, 'Delhi');
INSERT INTO students VALUES (2, 'Kartik', 22, 'Mumbai');
INSERT INTO students VALUES (3, 'Rahul', 20, 'Bangalore');
-- ... and 10,000 total rows using generate_series
```

```
Resulting database footprints:
- PostgreSQL: ~1.1MB table space (excluding WAL segments)
- SQLite: ~400KB single database file
```

SQLite has a smaller storage footprint because it avoids the MVCC metadata headers (`xmin`, `xmax`) stored with each PostgreSQL row.

![Database Size Comparison Screenshot](db_size_experiment.png)

### Experiment 2: Concurrent Writes

Testing concurrent write operations using a Python script:

```python
# Simulated workload: 5 writers + 5 readers
import threading

def writer_thread(db, thread_id):
    for i in range(100):
        db.execute(f"INSERT INTO test VALUES ({thread_id * 1000 + i}, 'data')")
```

- **PostgreSQL**: Handled concurrent connections without conflicts.
- **SQLite (Journal Mode)**: Encountered locked errors because writes lock the entire database.
- **SQLite (WAL Mode)**: Handled concurrent reads alongside a single writer, but concurrent write operations were serialized.

![Concurrent Access Test Screenshot](concurrent_access_test.png)

### Experiment 3: Query Plan Analysis

Analyzing a simple filter query:

```sql
EXPLAIN ANALYZE SELECT * FROM students WHERE city = 'Delhi' AND age > 25;
```

```
Plan without index (Sequential scan):
Seq Scan on students  (cost=0.00..230.00 rows=1012 width=36)
                      (actual time=0.015..2.134 rows=987 loops=1)
  Filter: ((age > 25) AND (city = 'Delhi'::text))
```

![Query Plan Sequential Scan without index](query_plan_seq_scan_no_index.png)

Adding a composite index improves query performance:

```sql
CREATE INDEX idx_city_age ON students(city, age);
EXPLAIN ANALYZE SELECT * FROM students WHERE city = 'Delhi' AND age > 25;
```

```
Plan with index (Index scan):
Index Scan using idx_city_age on students  (cost=0.29..41.23 rows=1012 width=36)
                                           (actual time=0.023..0.456 rows=987 loops=1)
  Index Cond: ((city = 'Delhi'::text) AND (age > 25))
Execution time: 0.567 ms (vs 2.267 ms without index)
```

![Query Plan Sequential Scan with index](query_plan_seq_scan_index.png)

---

## 6. Key Learnings

1. **Architecture matches the target use case.** PostgreSQL is designed for concurrent, multi-user web applications, while SQLite is optimized for simple, embedded local environments.
2. **Locks vs MVCC determines write concurrency.** MVCC allows concurrent reads and writes by keeping multiple row versions, but it requires background vacuuming. SQLite uses simpler locks to avoid vacuum overhead at the cost of concurrency.
3. **SQLite is a robust database engine.** It is widely deployed across mobile operating systems, browsers, and embedded systems, demonstrating its reliability at scale.
4. **Different table storage styles impact lookups.** PostgreSQL's heap files keep index sizes small but require secondary lookups. SQLite's B+ tree structure enables faster primary key queries.

---
