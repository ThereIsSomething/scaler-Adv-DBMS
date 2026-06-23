**Name:** Nitish Kumar Bhambu  
**Roll No:** 24BCS10589

---

# MySQL / InnoDB Storage Engine — Deep Dive

> MySQL/InnoDB padhte time jab maine isko PostgreSQL se compare kiya toh real differences samajh aaye. Relational databases hone ke baad bhi inka internal working bilkul alag hai — jaise do gaadiyan bahar se toh same lagein but unka engine complete design wise different ho.

---

## 1. Problem Background

MySQL ko start kiya tha Michael Widenius aur David Axmark ne, aur 1995 mein launch kiya. Start mein ye sirf simple queries ke liye super fast tha but transactions, crash recovery, aur foreign keys jaise critical features isme nahi the.

Is problem ko solve karne ke liye Heikki Tuuri ne 1990s ke late years mein **InnoDB** develop kiya, jo ki ek pluggable storage engine tha. InnoDB ne MyISAM (MySQL ka original engine) ki saari kamiyan dur kin aur ye features laaye:
- ACID transactions support
- Row-level locking (MyISAM mein table-level thi)
- Foreign key constraint checks
- Reliable crash recovery
- MVCC (Multi-Version Concurrency Control)

Year 2010 mein MySQL 5.5 ke release ke baad se InnoDB ko **default** engine bana diya gaya. Ab jab bhi log MySQL ki baat karte hain, unka reference mostly InnoDB se hi hota hai.

Ek major difference jo yahan dikhta hai wo hai MySQL ka **pluggable storage engine architecture**. Iska matlab tum ek hi DB server pe alag-alag tables ke liye dynamic engines select kar sakte ho, jabki PostgreSQL mein aisi flexibility nahi hai, wahan built-in single storage engine hi hota hai.

```
MySQL Architecture:

┌─────────────────────────────────────────────┐
│  MySQL Server Layer                          │
│  ┌─────────┐ ┌───────────┐ ┌────────────┐  │
│  │ Parser  │ │ Optimizer │ │ Executor   │  │
│  └─────────┘ └───────────┘ └──────┬─────┘  │
│                                    │         │
│  ──────── Storage Engine API ──────┤─────── │
│                                    │         │
│  ┌──────────┐ ┌──────────┐ ┌─────▼──────┐  │
│  │ MyISAM   │ │ Memory   │ │  InnoDB    │  │
│  │ (legacy) │ │ (in-mem) │ │ (default)  │  │
│  └──────────┘ └──────────┘ └────────────┘  │
└─────────────────────────────────────────────┘
```

Ye pluggable model flexible lagta hai but iska drawback ye hai ki server layer aur engine layer ke overlap mein optimization limitations aa jati hain, jo tight-coupled systems (jaise Postgres) directly optimize kar lete hain.

---

## 2. Architecture Overview

### InnoDB ka inner architecture block diagram:

```
┌──────────────────────────────────────────────────────────────┐
│                     InnoDB Architecture                       │
│                                                                │
│  ┌──────────────────── In-Memory ──────────────────────────┐ │
│  │                                                          │ │
│  │  ┌─────────────────────────────────────────────────┐    │ │
│  │  │              Buffer Pool                         │    │ │
│  │  │  ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐           │    │ │
│  │  │  │Data  │ │Index │ │Undo  │ │Insert│           │    │ │
│  │  │  │Pages │ │Pages │ │Pages │ │Buffer│           │    │ │
│  │  │  └──────┘ └──────┘ └──────┘ └──────┘           │    │ │
│  │  │                                                  │    │ │
│  │  │  Adaptive Hash Index    Change Buffer            │    │ │
│  │  └─────────────────────────────────────────────────┘    │ │
│  │                                                          │ │
│  │  ┌──────────────┐  ┌───────────┐  ┌──────────────┐     │ │
│  │  │ Log Buffer   │  │ Additional│  │ Data Dict    │     │ │
│  │  │ (redo log    │  │ Memory    │  │ Cache        │     │ │
│  │  │  buffer)     │  │ Pools     │  │              │     │ │
│  │  └──────────────┘  └───────────┘  └──────────────┘     │ │
│  └──────────────────────────────────────────────────────────┘ │
│                                                                │
│  ┌──────────────────── On Disk ────────────────────────────┐ │
│  │                                                          │ │
│  │  ┌────────────┐  ┌────────────┐  ┌────────────────┐    │ │
│  │  │ System     │  │ Redo Logs  │  │ Undo           │    │ │
│  │  │ (ibdata1)  │  │ (ib_log-   │  │ Tablespace     │    │ │
│  │  │            │  │  file0/1)  │  │                │    │ │
│  │  └────────────┘  └────────────┘  └────────────────┘    │ │
│  │                                                          │ │
│  │  ┌────────────┐  ┌────────────┐  ┌────────────────┐    │ │
│  │  │ Per-table  │  │ General    │  │ Temp           │    │ │
│  │  │ Tablespace │  │ Tablespace │  │ Tablespace     │    │ │
│  │  │ (.ibd)     │  │            │  │                │    │ │
│  │  └────────────┘  └────────────┘  └────────────────┘    │ │
│  │                                                          │ │
│  │  ┌────────────┐                                         │ │
│  │  │ Doublewrite│                                         │ │
│  │  │ Buffer     │                                         │ │
│  │  └────────────┘                                         │ │
│  └──────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────┘
```

Pehle ye diagram dekhne par clear nahi tha, par detailed analysis se ab har module ka function samajh aa gaya hai. Chalo points mein isko divide karke explore karte hain.

---

## 3. Internal Design

### 3.1 Clustered Index — InnoDB ki Identity

Ye InnoDB engine ka fundamental property hai aur PostgreSQL se iska sabse primary variance isi part mein hai.

**InnoDB ke andar har physical table actual mein ek clustered index hi hoti hai.** Rows ka biological arrangement primary key values ke direct logical order mein B+ tree pattern par storage mein save hota hai.

```
Clustered Index (Primary Key = id):

               ┌──────────────────┐
               │   Root Page       │
               │  [50]  [100]      │
               │ /     |      \    │
               └──┬────┬──────┬──┘
                  │    │      │
      ┌───────────┘    │      └───────────┐
      │                │                   │
 ┌────▼────────┐ ┌─────▼───────┐ ┌────────▼───────┐
 │ Leaf Page 1  │ │ Leaf Page 2  │ │ Leaf Page 3    │
 │              │ │              │ │                 │
 │ id=1:  data  │ │ id=51: data  │ │ id=101: data   │
 │ id=2:  data  │ │ id=52: data  │ │ id=102: data   │
 │ id=3:  data  │ │ id=53: data  │ │ id=103: data   │
 │ ...          │ │ ...          │ │ ...             │
 │ id=49: data  │ │ id=99: data  │ │ id=150: data   │
 │              │ │              │ │                 │
 │  next→Page2  │ │  next→Page3  │ │  next→NULL     │
 └──────────────┘ └──────────────┘ └────────────────┘

ACTUAL ROW DATA is stored IN the leaf pages!
```

PostgreSQL se dynamic comparison:

```
PostgreSQL (Heap + Separate Index):

B-Tree Index:                          Heap (unordered):
┌──────────┐                    ┌───────────────────────┐
│ Root     │                    │ Page 0:               │
│ [50][100]│                    │  row(id=42, ...)      │
│ └──┬───┬──┘                    │  row(id=7, ...)       │
│    │   │                        │  row(id=99, ...)      │
│ ┌──▼───▼──┐                    │ Page 1:               │
│ │ Leaf:    │                    │  row(id=1, ...)       │
│ │ id=1→(1,3)│──pointer──→      │  row(id=85, ...)      │
│ │ id=2→(0,2)│──pointer──→      │  row(id=3, ...) ← here│
│ │ ...      │                    │ ...                    │
│ └──────────┘                    └───────────────────────┘
```

Kya difference hai isme?

- **InnoDB:** primary key search karne pe page levels traverse hote hi sidhe records mil jaate hain. Faltu disk seek lookup nahi lagta. Very fast!
- **PostgreSQL:** index scan pe physical address pointer (TID) return hoga, fir execution layer heap files check karegi page metadata locate karne ke liye. Do cycles lagte hain (heap page fetch bolte hain isko).

Agar query run karein `SELECT * FROM users WHERE id = 12345`:
- InnoDB: B-Tree scan for ID 12345 directly targets leaf blocks, bringing columns along with the index key.
- PostgreSQL: B-Tree finds key value 12345 -> maps to pointer -> reads block reference -> pulls values from heap segment.

Data size bada ho toh ye design performance pe heavy impact daalta hai.

### 3.2 Secondary Indexes

Secondary index operations kaise execute hote hain InnoDB mein?

```
InnoDB Secondary Index (on 'email' column):

            ┌──────────────────┐
            │   Root Page       │
            │  [jjj@]  [sss@]  │
            └──┬────────┬──────┘
               │        │
     ┌─────────┘        └──────────┐
     │                              │
┌────▼───────────┐    ┌────────────▼─────┐
│ Leaf Page       │    │ Leaf Page         │
│                 │    │                   │
│ aaa@.. → PK=5  │    │ jjj@.. → PK=51  │
│ bbb@.. → PK=12 │    │ kkk@.. → PK=3   │
│ ccc@.. → PK=87 │    │ lll@.. → PK=200 │
│                 │    │                   │
└─────────────────┘    └──────────────────┘

Secondary index stores: indexed_column_value → PRIMARY KEY value
NOT the actual row data, NOT a pointer to disk location

So to read full row:
1. Search secondary index → find PK value
2. Search clustered index using PK → find actual data
(This is called a "double lookup" or "bookmark lookup")
```

Postgres internals comparison:
- **PostgreSQL:** secondary indexes direct physical reference TID store karte hain.
- **InnoDB:** secondary leaf primary key value carry karte hain (logical location representation).

Logical mappings ka ek clean advantage ye hai ki jab table partition split, row shifts ya data alignments change hote hain, secondary index mapping update karne ki mechanical cost negligible ho jaati hai kyunki static PK reference update nahi karna padta.

### 3.3 Buffer Pool

InnoDB buffer pool concepts PostgreSQL ke shared buffers space se coordinate hote hain — data memory pools ke structures.

```
InnoDB Buffer Pool:
┌──────────────────────────────────────────────────┐
│                                                    │
│  Default: 128MB (production mein 70-80% RAM)      │
│  Page size: 16KB (PostgreSQL = 8KB)               │
│                                                    │
│  Uses Modified LRU List:                           │
│  ┌────────────────────────────────────────────┐   │
│  │        Young Sublist (hot end)              │   │
│  │   [page A] [page B] [page C] ...          │   │
│  │        ↕ (midpoint — ~5/8 from head)       │   │
│  │        Old Sublist (cold end)              │   │
│  │   [page X] [page Y] [page Z] ...          │   │
│  │        └────────────────────────────────────────────┘   │
│                                                    │
│  New pages are inserted at the MIDPOINT,           │
│  not at the head! (midpoint insertion strategy)   │
│                                                    │
│  Why? To prevent full table scans from              │
│  flushing the entire buffer pool!                  │
│                                                    │
└──────────────────────────────────────────────────┘
```

Ye custom midpoint mapping simple but high utility fix hai. Socho agar `SELECT * FROM large_log_table` run kiya, toh binary search ya linear fetch se complete data read hoga. Agar plain LRU model chale toh recent logs saare active cached pages ko clean swap kar denge. Midpoint system se nayi incoming entries direct list ke center (Old sublist) mein dump hoti hain taaki existing active data secure rahe.

Postgres is loop protection ko Clock Sweep model ke relative use patterns se execute karta hai.

### 3.4 Undo Logs — Missing in PostgreSQL!

InnoDB memory model is transaction version structure pe chalta hai, jo Postgres mein dynamic status checking handles karta hai. Jab koi table row modified hoti hai:

```
InnoDB UPDATE process:

BEFORE: Row in clustered index
┌─────────────────────────────┐
│ id=1, name='Kartik', bal=5000│
└─────────────────────────────┘

UPDATE accounts SET bal=3000 WHERE id=1;

Step 1: Save old version in UNDO LOG
┌─────────────────────────────────────┐
│ Undo Log Record:                     │
│ "Row id=1 had: name='Kartik',bal=5000│
│  Transaction: T200                   │
│  Previous undo: pointer             │
└─────────────────────────────────────┘

Step 2: IN-PLACE update the actual row
┌─────────────────────────────────────────────┐
│ id=1, name='Kartik', bal=3000  ← CHANGED!   │
│ DB_TRX_ID = T200                            │
│ DB_ROLL_PTR → undo log record               │
└─────────────────────────────────────────────┘
```

Postgres directly target coordinates append-only patterns pe save karta hai:

```
PostgreSQL UPDATE process:

BEFORE: Heap
┌─────────────────────────────────────┐
│ Tuple A: xmin=100, xmax=0          │
│ data = (1, 'Kartik', 5000)          │
└─────────────────────────────────────┘

UPDATE accounts SET bal=3000 WHERE id=1;

AFTER: Heap (old version stays, new version added)
┌─────────────────────────────────────┐
│ Tuple A: xmin=100, xmax=200 (DEAD) │
│ data = (1, 'Kartik', 5000)          │
├─────────────────────────────────────┤
│ Tuple B: xmin=200, xmax=0 (LIVE)   │
│ data = (1, 'Kartik', 3000)          │
└─────────────────────────────────────┘
```

Comparative differences:

| Metric | InnoDB | PostgreSQL |
|--------|--------|------------|
| Update System | In-place modifications | Append new tuple version |
| Old Versions Location | Undo tablespace structure | Standard database heap files |
| Garbage Cleanup | Purge threads drop old undo logs | VACUUM scans table blocks |
| Space Bloating | Very minimal (in-place edits) | High rate of dead space |
| VACUUM dependency | None | High importance for autovacuum |
| Temporary storage | Separate Undo Segment area | N/A |

PostgreSQL approach simple aur robust hai but table bloat control karna costly padta hai. InnoDB in-place writes manages karta hai toh table bloat is minimal, but undo logging setup complex ho jata hai.

### 3.5 Redo Logs

Redo logging write logs (WAL) ke concept pe based hai — changes update updates transaction logs ke sequential dump ke baad finalize honge.

```
InnoDB Redo Log:

┌─────────────────────────────────────────────┐
│  Redo Log Files (circular):                  │
│                                               │
│    ib_logfile0         ib_logfile1            │
│  ┌──────────────┐   ┌──────────────┐        │
│  │              │   │              │        │
│  │  Redo records│──→│  Redo records│──┐     │
│  │              │   │              │  │     │
│  └──────────────┘   └──────────────┘  │     │
│         ▲                              │     │
│         └──────────────────────────────┘     │
│              (wraps around — circular)       │
│                                               │
│  Log Sequence Number (LSN) tracks position   │
│                                               │
│  Key difference from PostgreSQL WAL:          │
│  - Fixed size, circular (PG WAL = growing)   │
│  - Must checkpoint before logs wrap around!  │
└─────────────────────────────────────────────┘
```

Circular file handling direct constraints create karti hai. Agar buffers write load se fill ho jayein bina clean flush check ke, toh checkpoints pending locks trigger karenge. Is system blocking state ko production environment mein "Redo Log Stalls" kehte hain.

#### Double Logging logic: Undo aur Redo dono kyun?

In-place writes aur transaction rollbacks dono handle karne ke liye in dono blocks ki zaroorat hoti hai:

```
Scenario: Transaction T1 does UPDATE, then CRASH happens

Case 1: T1 was COMMITTED before crash
→ Redo log has details.
→ Recovery: Apply Redo logs (Roll Forward)
→ Result: Committed data persists.

Case 2: T1 was UNCOMMITTED before crash
→ Database files might have partially updated page data.
→ Recovery: Apply Undo logs (Roll Back)
→ Result: Restore original state to keep consistency.
```

```
InnoDB Crash Recovery Process:
┌─────────────────────────────────────────────┐
│ Step 1: REDO Phase (Roll Forward)            │
│   Apply all redo logs after checkpoint.      │
│   → DB holds all transaction data.           │
│                                               │
│ Step 2: UNDO Phase (Roll Back)               │
│   Scan and rollback active transactions      │
│   using the undo chain.                      │
└─────────────────────────────────────────────┘
```

Postgres works differently here because it doesn't do in-place updates. Dead tuples keep the old state in heap blocks until cleanup, avoiding explicit undo recovery.

### 3.6 Row Locking aur Gap Locks

MyISAM table-wide blocks use karta tha. InnoDB row-level isolation manage karta hai to make operations efficient.

```
InnoDB Locks:

Shared (S) Lock: Reads allowed concurrently.
Exclusive (X) Lock: Direct write blocker.

Record Lock: Locks index keys.
Gap Lock: Locks ranges between keys (block insert space).
Next-Key Lock: Combination of Record and Gap lock.
```

Gap locking explanation through logic:

```
Table: students (id INT PRIMARY KEY)
Current keys: 10, 20, 30, 40
Range representation: 10 --- 20 --- 30 --- 40

If transaction T1 runs:
  SELECT * FROM students WHERE id BETWEEN 15 AND 25 FOR UPDATE;

Next-Key mapping:
- Locks key 20 and the interval [10, 20).
- Gap locks the range (20, 30).

If T2 runs:
  INSERT INTO students VALUES (22, ...);  ← BLOCKED (Gap lock active)
  INSERT INTO students VALUES (25, ...);  ← BLOCKED (Gap lock active)
  INSERT INTO students VALUES (35, ...);  ← OK
```

Without locking gaps, concurrency issues like Phantom Reads occur when queries scan ranges multiple times. PostgreSQL deals with this by using snapshots and transaction visibility rules.

### 3.7 InnoDB MVCC — Oracle Style

Oracle architecture styles inspire InnoDB MVCC properties.

```
InnoDB Row Hidden Columns:

Every row has 3 hidden fields:
┌──────────┬────────────┬──────────────┐
│DB_TRX_ID │DB_ROLL_PTR │ DB_ROW_ID    │
│          │            │ (if no PK)   │
│Last txn  │Pointer to  │Auto-generated│
│that      │undo log    │row ID        │
│modified  │record      │              │
│this row  │            │              │
└──────────┴────────────┴──────────────┘

When reading:
1. Check DB_TRX_ID visibility.
2. If match -> fetch record.
3. If no match -> track DB_ROLL_PTR backwards.
4. Scan undo blocks for visible version.
```

```
Where old versions live:

PostgreSQL:
┌─────────────────────────┐
│ Heap Page:               │
│  [Old v1] [Old v2] [New]│  ← Same segment bloat
└─────────────────────────┘

InnoDB:
┌───────────────┐     ┌──────────────────┐
│ Data Page:     │     │ Undo Tablespace: │
│  [Current ver] │     │  [Old v1] [Oldv2]│  ← Segregated
└───────────────┘     └──────────────────┘
```

---

## 4. Design Trade-offs

### Clustered Index vs Heap Index Layouts

```
Clustered (InnoDB):
+ Faster primary key searches.
+ Ordered ranges speed up primary scans.
- Secondary keys have a two-step lookup.
- Random keys (like UUIDs) cause index splits.

Heap (PostgreSQL):
+ Single read lookup for all indexes.
+ Unordered inserts are fast.
- Primary key lookup is slower because of heap seeks.
- Full table scans read the entire heap.
```

### In-Place vs Append-Only

| Parameter | InnoDB (In-Place) | PostgreSQL (Append-only) |
|--------|-------------------|--------------------------|
| Update Write Load | Low page amplification | High version amplification |
| Space Fragmentation | Controlled table sizes | High bloating risks |
| Cleanup Cost | Background Purge | Intensive Autovacuum |
| Recovery System | Complex REDO + UNDO | Simple Redo logs replay |

---

## 5. Experiments / Observations

### Experiment 1: Clustered Index Performance

```sql
-- InnoDB (MySQL) Setup
CREATE TABLE orders_innodb (
    order_id INT PRIMARY KEY AUTO_INCREMENT,
    customer_id INT,
    amount DECIMAL(10,2),
    order_date DATE,
    INDEX idx_customer (customer_id)
) ENGINE=InnoDB;

-- Primary lookup execution:
SELECT * FROM orders_innodb WHERE order_id = 500000;
-- Execution time: ~0.3ms

-- Secondary lookup execution:
SELECT * FROM orders_innodb WHERE customer_id = 12345;
-- Execution time: ~0.8ms
```

Direct index key scans are faster than searching via secondary indexes because of the extra bookmark lookups.

![Clustered Index Benefit Screenshot](clustered_index_benefit.png)

### Experiment 2: Auto-Increment vs UUID Keys

Comparing auto-increment PKs against random keys:

```sql
-- Table 1: Auto increment keys
CREATE TABLE t1 (
    id INT AUTO_INCREMENT PRIMARY KEY,
    data VARCHAR(100)
) ENGINE=InnoDB;

-- Table 2: UUID keys
CREATE TABLE t2 (
    id CHAR(36) PRIMARY KEY,
    data VARCHAR(100)
) ENGINE=InnoDB;
```

```
Metrics comparison on 500K inserts:
┌──────────────────┬───────────────┬──────────────┐
│ Metric            │ AUTO_INCREMENT│ UUID          │
├──────────────────┼───────────────┼──────────────┤
│ Time taken        │ ~15 seconds   │ ~45 seconds  │
│ File size         │ ~50MB         │ ~85MB        │
│ Index Splits      │ Minimal       │ High         │
│ Memory Cache      │ Clean         │ Fragmented   │
└──────────────────┴───────────────┴──────────────┘
```

Auto-increment IDs write sequentially to the rightmost leaf page, preventing random page splits. UUIDs insert randomly, causing cache eviction and page overhead.

![UUID vs Auto-Increment Screenshot](uuid_vs_auto_increment.png)

### Experiment 3: Undo Log behavior with active snapshots

```sql
-- Terminal 1 (Nitish):
START TRANSACTION;
SELECT * FROM big_table WHERE id = 1;

-- Terminal 2 (Kartik):
-- Run large batch updates (100,000 modifications)

-- Check Undo stats:
SELECT COUNT AS current_undo_recs 
FROM information_schema.INNODB_METRICS 
WHERE NAME = 'trx_rseg_history_len';
```

Undo history length increases because transactions keep old snapshots active, preventing purge threads from cleaning undo blocks.

![Undo Log Growth Screenshot](undo_log_growth.png)

### Experiment 4: Deadlocks in InnoDB

```sql
-- Session 1 (Nitish):
START TRANSACTION;
UPDATE accounts SET bal = bal - 100 WHERE id = 1;

-- Session 2 (Kartik):
START TRANSACTION;
UPDATE accounts SET bal = bal - 50 WHERE id = 2;

-- Session 1 (Nitish):
UPDATE accounts SET bal = bal + 100 WHERE id = 2; -- Locked, waits for Session 2

-- Session 2 (Kartik):
UPDATE accounts SET bal = bal + 50 WHERE id = 1; -- Deadlock detected!
```

![Deadlock Detection](deadlock_detection.png)

InnoDB background monitor tracks waits-for relationships and terminates one transaction to resolve the deadlock.

---

## 6. Key Learnings

1. **Clustered index choice determines base performance.** Clustered table design makes key choices critical. Sequential IDs yield clean B+ trees, while random strings (like UUID v4) degrade write speed.
2. **Undo logs decouple storage and recovery.** By keeping old states in undo tablespaces, InnoDB updates in-place without bloating table files. This reduces the need for heavy background vacuum processes.
3. **Locks protect ranges, not just rows.** Gap locks prevent phantom insertions, but they can lower write concurrency. This trade-off must be managed in high-throughput applications.
4. **Midpoint cache insertion keeps memory efficient.** Pointing incoming disk pages to the center of the LRU queue prevents occasional full-table scans from clearing hot cache pages.
5. **No database design is perfect.** Database architecture is all about trade-offs. Clustered indexes speed up key searches but slow down secondary indexes. In-place writes prevent bloat but require complex undo logs.

---
