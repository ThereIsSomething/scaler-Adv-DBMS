**Name:** Nitish Kumar Bhambu  
**Roll No:** 24BCS10589

---

# PostgreSQL Internals — Deep Dive

> Ye wala topic mujhe sabse zyada interesting laga but sabse complex bhi. PostgreSQL ke source code structure ko explore kiya toh ek different level ka architecture samajh aaya. Jo concepts seekhe, unko main yahan describe kar raha hoon with diagrams aur clear examples.

---

## 1. Problem Background

PostgreSQL ek mature aur complete feature-rich relational database engine hai. But iske basic storage functions ke peeche ka internal design system sabse mechanical part hai.

Pehle main postgres mein simple queries execute karta tha like `CREATE TABLE`, `INSERT` aur basic `SELECT`. Lekin internals study karne par pata chala ki ek query `SELECT * FROM users WHERE id = 5` execute hone ke peeche kitne layers active hote hain — buffer managers target pages trace karte hain, index structures check hote hain, MVCC transactions visibility filters generate karti hain, aur optimizer scan choices choose karta hai.

Is document mein ye char core engine concepts cover hain:
1. **Buffer Manager** — memory buffer aur actual storage drive ke beech ka core coordinator
2. **B-Tree (nbtree)** — table indexes ka storage aur search execution
3. **MVCC** — writes aur reads ko coordinate karne ka concurrency model
4. **WAL (Write-Ahead Logging)** — database reliability check crashes ke dauran

Aakhir mein, dynamic execution flows check karne ke liye `EXPLAIN ANALYZE` metrics runtime tracking ka implementation test hai.

---

## 2. Architecture Overview

Postgres execution flow block model:

```
┌──────────────────────────────────────────────────────────────────┐
│                     PostgreSQL Backend Process                    │
│                                                                    │
│  ┌─────────┐    ┌──────────────┐    ┌────────────┐               │
│  │ Parser  │───→│ Planner/     │───→│  Executor  │               │
│  │         │    │ Optimizer    │    │            │               │
│  └─────────┘    └──────────────┘    └─────┬──────┘               │
│                                           │                       │
│                                    ┌──────▼──────┐               │
│                                    │  Access     │               │
│                                    │  Methods    │               │
│                                    │ (heap,index)│               │
│                                    └──────┬──────┘               │
│                                           │                       │
│  ┌────────────────────────────────────────▼───────────────────┐  │
│  │                    Buffer Manager                          │  │
│  │  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐           │  │
│  │  │Page 1│ │Page 2│ │Page 3│ │Page 4│ │ ...  │           │  │
│  │  │      │ │      │ │(dirty)│ │      │ │      │           │  │
│  │  └──────┘ └──────┘ └──────┘ └──────┘ └──────┘           │  │
│  │               Shared Buffer Pool                          │  │
│  └───────────────────────┬───────────────────────────────────┘  │
│                          │                                       │
└──────────────────────────┼───────────────────────────────────────┘
                           │ read/write
                    ┌──────▼──────┐
                    │    Disk     │
                    │ ┌────────┐  │
                    │ │Data    │  │
                    │ │Files   │  │
                    │ ├────────┤  │
                    │ │WAL     │  │
                    │ │Files   │  │
                    │ └────────┘  │
                    └─────────────┘
```

Standard backend queries processing:
1. SQL query stream parsed hoti hai -> **Parser** create karta hai syntax trees.
2. **Planner/Optimizer** cost calculation choice mapping check karta hai using statistics databases (`pg_statistic` reference blocks).
3. **Executor** target query plans implement karta hai.
4. Data accesses call trigger karte hain -> **Access Methods** (heap/index scan routes).
5. Access logic coordinates blocks through the **Buffer Manager**.
6. Buffer Manager caches scan karta hai. Hit hone par page memory blocks return honge, block missing hone par storage disk access active hoga.

---

## 3. Internal Design

### 3.1 Buffer Manager

**Source path reference:** `src/backend/storage/buffer/`

Ye page request management system database memory and drive operations ke middle mein ek efficient caching system ki tarah function karta hai.

#### Shared Buffers

Postgres system shared RAM pools mein custom **shared buffer pool** structures run karta hai. Standard setup default 128MB space allocates karta hai, but large workloads memory scales settings 25% allocation target karti hain.

```
Shared Buffer Pool:
┌────────────────────────────────────────────────────────────┐
│                                                             │
│  ┌────────┐  ┌────────┐  ┌────────┐  ┌────────┐          │
│  │ Buffer │  │ Buffer │  │ Buffer │  │ Buffer │   ...     │
│  │ Desc 0 │  │ Desc 1 │  │ Desc 2 │  │ Desc 3 │          │
│  │        │  │        │  │        │  │        │          │
│  │ tag:   │  │ tag:   │  │ tag:   │  │ tag:   │          │
│  │(rel,   │  │(rel,   │  │(rel,   │  │(rel,   │          │
│  │ fork,  │  │ fork,  │  │ fork,  │  │ fork,  │          │
│  │ block) │  │ block) │  │ block) │  │ block) │          │
│  │        │  │        │  │        │  │        │          │
│  │flags:  │  │flags:  │  │flags:  │  │flags:  │          │
│  │dirty?  │  │dirty?  │  │dirty?  │  │dirty?  │          │
│  │valid?  │  │valid?  │  │valid?  │  │valid?  │          │
│  │pinned? │  │pinned? │  │pinned? │  │pinned? │          │
│  │refcount│  │refcount│  │refcount│  │refcount│          │
│  └───┬────┘  └───┬────┘  └───┬────┘  └───┬────┘          │
│      │           │           │           │                │
│  ┌───▼────┐  ┌───▼────┐  ┌───▼────┐  ┌───▼────┐         │
│  │ 8KB    │  │ 8KB    │  │ 8KB    │  │ 8KB    │          │
│  │ Page   │  │ Page   │  │ Page   │  │ Page   │   ...    │
│  │ Data   │  │ Data   │  │ Data   │  │ Data   │          │
│  └────────┘  └────────┘  └────────┘  └────────┘          │
│                                                            │
│  Hash Table: (relation_id, fork, block_num) → buffer_id   │
│  ┌─────────────────────────────────────────────────┐      │
│  │ hash(tag) → bucket → buffer descriptor          │      │
│  └─────────────────────────────────────────────────┘      │
│                                                            │
└────────────────────────────────────────────────────────────┘
```

Buffer descriptor arrays mapping details:
- **Buffer Descriptor** properties: mapping identity metadata records (relation info, active edits state, processes referencing locks).
- **Buffer Page** records: target 8KB physical storage pages.

Logical fetch sequences:

```
ReadBuffer(relation, block_number):
    1. Define key tag = (relation_id, fork_number, block_number)
    2. Check the lookup hash: is this page in memory?
       ├── YES → Pin the slot, return the data (CACHE HIT)
       └── NO  → EVICT/REPLACE execution:
                  a. Identify replacement candidate slot using sweep logic
                  b. Write candidate page to disk if flagged dirty
                  c. Read target blocks from disk into empty buffer slot
                  d. Update hash mapping tables with new tag references
                  e. Pin page and return data
```

#### Buffer Replacement — Clock Sweep Algorithm

Jab space limits clear ho jayein aur naye blocks memory mein drag karne hon, candidate slots selection **Clock Sweep** system se operate hota hai (LRU model variations ka simpler logic).

```
Clock Sweep Algorithm:

Buffer array structures mapped circularly:

         ┌──[0]──[1]──[2]──┐
         │                   │
        [7]     CLOCK       [3]
         │      HAND→       │
         └──[6]──[5]──[4]──┘

Each buffer tracks usage metrics (usage_count):

Accessing a page increments usage_count (cap at 5).
Replacement scans:
  1. Hand traverses target slots sequentially.
  2. For the evaluated slot:
     - If usage_count > 0 → decrement it and advance hand.
     - If usage_count == 0 AND unpinned → select this slot for EVICTION.
     - If pinned → skip slot and advance hand.
  3. Repeat loop until slot is chosen.
```

Ye implementation memory scans optimize karti hai. Active loops aur transactions ke cached pages select hone se bachte hain, aur dead pages quickly swap ho jaate hain. Lock costs aur array shifts control karne mein ye process simple LRU se better and highly performance friendly hai.

#### Background Writer (bgwriter)

Background process dirty blocks ko target system speed match karne ke liye drive directories pe clean writes chalata hai. Is processing se candidate sweeps ke dauran storage disk operations synchronous conflicts generate nahi karte.

```
bgwriter execution loop:
  while true:
    wait for bgwriter_delay duration (defaults to 200ms)
    scan shared buffer structures
    identify unpinned dirty pages having small usage counts
    write pages to disk (up to bgwriter_lru_maxpages limit)
    reset dirty status flag
```

---

### 3.2 B-Tree Implementation (nbtree)

**Source path reference:** `src/backend/access/nbtree/`

Index structures concurrent actions handle karne ke liye Lehman & Yao design parameters consume karte hain.

#### Index Page Layout

```
B-Tree Index Page Structure:

┌──────────────────────────────────────────┐
│           Page Header (24 bytes)          │
│  - pd_lsn, pd_flags                      │
├──────────────────────────────────────────┤
│        BTPageOpaqueData (Special)         │
│  - Sibling references (btpo_prev/next)   │
│  - Tree level metadata (btpo_level)      │
│  - Index flags indicators                │
├──────────────────────────────────────────┤
│      Line Pointers (Item IDs)            │
│  [ItemId 1] [ItemId 2] ...               │
├──────────────────────────────────────────┤
│           Free Space                      │
├──────────────────────────────────────────┤
│        Index Tuples (bottom-up)           │
│  ┌─────────────────────────┐             │
│  │ IndexTuple:             │             │
│  │  - key value(s)         │             │
│  │  - TID (table row loc)  │  ← leaf    │
│  │  OR                     │             │
│  │  - child page number    │  ← internal│
│  └─────────────────────────┘             │
└──────────────────────────────────────────┘
```

#### Search Path — Index Scan Execution

Example search index execution: `SELECT * FROM users WHERE email = 'kartik@example.com'` when B-Tree index matches parameters.

```
Search Path:

Step 1: Parse root page
┌─────────────────────────────┐
│ Root (level 2)               │
│ [aaa@... | jjj@... | sss@..]│
│ [ptr0|ptr1|ptr2|ptr3]       │
└─────────┬───────────────────┘
          │ 'kartik@...' >= 'jjj@...' 
          │ and < 'sss@...' -> follow ptr2
          ▼
Step 2: Traverse intermediate node
┌─────────────────────────────┐
│ Internal (level 1)           │
│ [jjj@... | mmm@... | ppp@..]│
│ [ptr0|ptr1|ptr2|ptr3]       │
└─────────┬───────────────────┘
          │ follow target branch pointer
          ▼
Step 3: Read leaf node blocks
┌─────────────────────────────────────┐
│ Leaf (level 0)                       │
│ [jack@..→TID(5,3)]                  │
│ [kartik@example.com→TID(12,7)] ← MATCH│
│ [megha@..→TID(8,1)]                 │
│                                      │
│ prev←[page 44]  next→[page 46]     │
└─────────────────────────────────────┘

Step 4: Fetch target tuple using physical pointer TID(12,7)
        (Block page reference 12, slot 7)
```

#### Insert Operations and Page Splits

Adding values when tree nodes are fully loaded:

```
Inserting keys into full pages:

1. Search leaf nodes to find insert location.
2. If space exists, insert entry.
3. If page is full, split the node.

Split execution:
BEFORE:
┌──────────────────────────────┐
│ Leaf Page (FULL)              │
│ [aaa] [bbb] [ccc] [ddd]     │
│ [eee] [fff] [ggg] [hhh]     │
└──────────────────────────────┘

AFTER:
┌───────────────┐    ┌───────────────┐
│ Old Leaf       │    │ New Leaf       │
│ [aaa] [bbb]   │───→│ [eee] [fff]   │
│ [ccc] [ddd]   │    │ [ggg] [hhh]   │
│               │    │ [kate] ← NEW  │
└───────────────┘    └───────────────┘
                          │
          Add key [eee] to parent index pointing to new node
```

Lehman & Yao structures employ "High Keys" in each node to define upper limits. This enables concurrent index lookups to navigate correctly without holding write locks down the path.

---

### 3.3 MVCC (Multi-Version Concurrency Control)

Concurrency control in postgres relies on tuple versioning.

#### Heap Tuple Versioning

Each record header defines execution tracking attributes:

```
┌──────────────────────────────────────────────┐
│ Tuple Header Fields for MVCC:                │
│                                               │
│ t_xmin = Creator transaction ID              │
│ t_xmax = Deleting/Updating transaction ID     │
│          (0 if active/live)                   │
│ t_cid  = Command index inside transaction    │
│ t_ctid = Logical pointer pointing to newer   │
│          record versions                      │
└──────────────────────────────────────────────┘
```

#### Versioning Flow Example

```
Initial State: Empty table structure "accounts"

T100: INSERT INTO accounts VALUES (1, 'Kartik', 5000);

Heap page blocks:
┌─────────────────────────────────────────┐
│ Tuple A:                                 │
│   xmin=100, xmax=0, data=(1,'Kartik',5000)│
│   t_ctid = (page0, item1) [self-pointer] │
└─────────────────────────────────────────┘
T100 commits. Tuple A is live and visible.
```

Executing updates:

```
T200: UPDATE accounts SET balance=3000 WHERE id=1;

Execution sequences:
1. Target Tuple A.
2. Write T200 id into Tuple A's xmax.
3. Append Tuple B containing updated values.
4. Link Tuple A's t_ctid to target Tuple B.

Page layout post-update:
┌─────────────────────────────────────────┐
│ Tuple A (OLD — dead once T200 commits):  │
│   xmin=100, xmax=200                    │
│   data=(1, 'Kartik', 5000)               │
│   t_ctid → Tuple B                      │
├─────────────────────────────────────────┤
│ Tuple B (NEW — live version):            │
│   xmin=200, xmax=0                      │
│   data=(1, 'Kartik', 3000)               │
│   t_ctid → itself                       │
└─────────────────────────────────────────┘
```

Both old and new versions exist simultaneously. Old entries are updated with an xmax value to define deletion transaction parameters.

#### Visibility Rules

Determining version matching rules for active transactions:

```
Visibility logic (simplified version):

Tuple visible to current reader transaction T if:

1. Creator xmin transaction is committed AND xmin is older than T's snapshot.
2. And either:
   a. xmax is set to 0 (active/unmodified).
   b. Deleting xmax transaction is aborted/not committed.
   c. Deleting xmax is committed but starts after T's snapshot was taken.
```

```
Timeline visualization:
────────────────────────────────────────────►
T100: INSERT (1,'Kartik',5000) → COMMIT
          T200: BEGIN
          T200: Read -> returns (1,'Kartik',5000)
                    T300: BEGIN
                    T300: UPDATE balance=3000 → COMMIT
          T200: Read AGAIN
          T200: Still returns (1,'Kartik',5000) (Snapshot holds T200 boundaries)
```

Snapshot isolation ensures that readers see a consistent view of the database. Active writers do not block readers.

#### VACUUM — Garbage Collection

Because updates are essentially appends, dead versions accumulate over time. The vacuum engine handles cleanup of these dead tuples:

```
VACUUM Execution:

BEFORE:
┌─────────────────────────────────┐
│ Tuple A: xmin=100, xmax=200    │ ← DEAD (no txn needs this)
│ Tuple B: xmin=200, xmax=300    │ ← DEAD
│ Tuple C: xmin=300, xmax=0      │ ← LIVE (current version)
└─────────────────────────────────┘

System checks active transaction states.
If no active transaction needs the dead tuples:

AFTER:
┌─────────────────────────────────┐
│ [FREE SPACE — reusable]        │
│ [FREE SPACE — reusable]        │
│ Tuple C: xmin=300, xmax=0      │ ← LIVE
└─────────────────────────────────┘
```

`VACUUM` doesn't reclaim storage space to the OS; it marks dead sectors in the page layout as reusable for new inserts. `VACUUM FULL` builds new files to reclaim space, but it locks the target tables. Production databases use background Autovacuum daemons tuned through scale parameters.

---

### 3.4 WAL (Write-Ahead Logging)

#### WAL Rule

**Write modifications to logs on disk before updating data blocks in memory.**

```
WAL Principle:
┌────────────────────────────────────────────────────┐
│  Before editing page files, append log details      │
│  to disk.                                          │
│  Commit operations only complete when log buffers   │
│  successfully write to disk.                       │
└────────────────────────────────────────────────────┘
```

If crash events occur during memory-to-disk flushes, recovery processes replay WAL entries to bring the database back to a consistent state.

#### WAL Record Structure

```
WAL Record:
┌──────────────────────────────────┐
│ Header:                           │
│  - xl_tot_len, xl_xid            │
│  - xl_prev (previous offset LSN) │
│  - xl_info, xl_rmid, xl_crc      │
├──────────────────────────────────┤
│ Data:                             │
│  - Block reference mappings      │
│  - Delta modification values     │
└──────────────────────────────────┘
```

#### LSN — Log Sequence Number

Each WAL record maps to an address called the **LSN (Log Sequence Number)**, representing a byte offset in the WAL stream.

```
WAL Records Stream:
┌────────┬────────┬────────┬────────┬────────┐
│Record 1│Record 2│Record 3│Record 4│Record 5│
│LSN:0/10│LSN:0/80│LSN:0/F0│LSN:1/20│LSN:1/90│
└────────┴────────┴────────┴────────┴────────┘

Database pages store active LSN limits in headers (pd_lsn).
Recovery:
- If page_lsn < WAL_record_lsn → Apply change (REDO).
- If page_lsn >= WAL_record_lsn → Skip change.
```

#### Checkpointing

To keep recovery times reasonable, checkpoint operations write all in-memory changes to disk periodically.

```
Checkpoint flow:
1. Register target LSN starting coordinates.
2. Write all dirty memory pages to disk.
3. Write checkpoint record details to WAL.
4. Recycle old WAL segments.
```

Tuning params `checkpoint_timeout` and `max_wal_size` control the frequency of checkpoints. Frequent checkpoints reduce recovery time but increase disk write load.

#### Crash Recovery

```
Recovery steps:
1. Server starts up after crash.
2. Locate last checkpoint record in WAL.
3. Scan forward from that point.
4. Compare page LSNs with WAL record LSNs and apply changes (REDO).
5. Recovered and ready to accept connections.
```

Postgres doesn't need an undo phase during recovery. MVCC visibility rules automatically hide changes from transactions that didn't commit before the crash.

---

## 4. Design Trade-offs

### Buffer Manager Trade-offs

| Parameter | Advantage | Drawback |
|---|---|---|
| Fixed size RAM allocation | Predictable memory footprint | Tuning required; too small hurts hits, too big impacts OS filesystem cache |
| Clock Sweep eviction | Simple O(1) performance | Less precise than true LRU |
| 8KB Page size | Balanced layout | Suboptimal for small point lookups or massive sequential scans |

### MVCC Trade-offs

```
PostgreSQL MVCC (Append-only style):
PROS:
+ Reads and writes don't block each other.
+ Simpler REDO-only recovery.
+ Direct snapshot availability.

CONS:
- Dead versions cause table bloat.
- Autovacuum write overhead.
- Primary key updates require index entry updates.
```

---

## 5. Experiments / Observations

### EXPLAIN ANALYZE execution details

Setup tables structure containing test records:

```sql
-- Schema setup
CREATE TABLE departments (
    dept_id SERIAL PRIMARY KEY,
    dept_name VARCHAR(50)
);

CREATE TABLE employees (
    emp_id SERIAL PRIMARY KEY,
    name VARCHAR(100),
    dept_id INTEGER REFERENCES departments(dept_id),
    salary INTEGER
);

CREATE TABLE projects (
    project_id SERIAL PRIMARY KEY,
    project_name VARCHAR(100),
    lead_emp_id INTEGER REFERENCES employees(emp_id),
    budget INTEGER
);

-- Load sample: 50,000 employees, 20 departments, 500 projects
-- Sample dataset includes Nitish (dept 5), Kartik (dept 3).
```

Target query:

```sql
EXPLAIN (ANALYZE, BUFFERS, FORMAT TEXT) 
SELECT d.dept_name, 
       COUNT(e.emp_id) as emp_count, 
       AVG(e.salary) as avg_salary,
       COUNT(p.project_id) as project_count
FROM departments d
JOIN employees e ON e.dept_id = d.dept_id
LEFT JOIN projects p ON p.lead_emp_id = e.emp_id
WHERE e.salary > 50000
GROUP BY d.dept_name
ORDER BY avg_salary DESC;
```

Execution trace output details:

```
Sort  (cost=3245.67..3245.72 rows=20 width=78) 
      (actual time=87.234..87.241 rows=20 loops=1)
  Sort Key: (avg(e.salary)) DESC
  Sort Method: quicksort  Memory: 26kB
  Buffers: shared hit=1823 read=342
  ->  HashAggregate  (cost=3244.89..3245.14 rows=20 width=78)
      (actual time=87.198..87.215 rows=20 loops=1)
        Group Key: d.dept_name
        Batches: 1  Memory Usage: 24kB
        ->  Hash Left Join  (cost=1234.56..3100.23 rows=28932 width=26)
            (actual time=12.456..72.891 rows=24856 loops=1)
              Hash Cond: (e.emp_id = p.lead_emp_id)
              ->  Hash Join  (cost=120.50..1789.34 rows=25123 width=22)
                  (actual time=1.234..35.678 rows=24856 loops=1)
                    Hash Cond: (e.dept_id = d.dept_id)
                    ->  Seq Scan on employees e  (cost=0.00..1400.00 rows=25123 width=16)
                        (actual time=0.012..18.456 rows=24856 loops=1)
                          Filter: (salary > 50000)
                          Rows Removed by Filter: 25144
                          Buffers: shared hit=1200 read=180
                    ->  Hash  (cost=120.00..120.00 rows=20 width=14)
                        (actual time=0.089..0.091 rows=20 loops=1)
                          Buckets: 1024  Batches: 1  Memory Usage: 9kB
                          ->  Seq Scan on departments d  ...
              ->  Hash  (cost=789.00..789.00 rows=500 width=8)
                  (actual time=3.456..3.458 rows=500 loops=1)
                    ->  Seq Scan on projects p  ...
Planning Time: 0.456 ms
Execution Time: 87.345 ms
```

![Terminal Analysis Output Screenshot](explain_analyze_output.png)

#### Output Analysis:

1. **Join Strategy choice:** The planner selects Hash Joins because `departments` (20 rows) and `projects` (500 rows) tables are small, allowing their hash tables to fit easily in memory.
2. **Seq Scan decision:** Despite the filter on `salary`, a sequential scan is performed on the `employees` table because about 50% of the rows match the filter. In this case, a sequential scan is faster than an index scan due to sequential I/O patterns.
3. **Buffer stats:** `shared hit=1823 read=342` indicates an 84% cache hit ratio (1823 pages fetched from memory, 342 from disk). Production environments typically target hit ratios over 99%.

#### pg_statistic data

The `ANALYZE` command updates the statistics used by the query planner:

```sql
ANALYZE employees;

-- Querying the statistics:
SELECT attname, null_frac, avg_width, n_distinct,
       most_common_vals, most_common_freqs, histogram_bounds
FROM pg_stats 
WHERE tablename = 'employees' AND attname = 'salary';
```

The planner uses these statistics to choose the best join algorithms, determine index vs sequential scan usage, and estimate row counts. Outdated statistics can lead to poor query plans.

---

## 6. Key Learnings

1. **Shared Buffers sizing is critical.** Tuning `shared_buffers` and monitoring hit ratios are essential first steps in optimizing PostgreSQL performance.
2. **MVCC requires regular vacuuming.** The append-only versioning model prevents read-write blocking, but it requires regular vacuuming to prevent table bloat and transaction ID wraparound.
3. **WAL simplifies recovery.** PostgreSQL avoids the need for complex undo logs during recovery because MVCC visibility rules automatically handle transaction rollbacks.
4. **Up-to-date statistics are essential.** The query planner depends on accurate statistics from `ANALYZE` commands to choose the most efficient execution plans.
5. **Lehman & Yao B-Trees optimize concurrency.** The B-tree implementation uses sibling pointers and page high keys to allow concurrent read and write operations without heavy locking.

---
