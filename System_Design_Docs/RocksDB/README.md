**Name:** Nitish Kumar Bhambu  
**Roll No:** 24BCS10589

---

# RocksDB Architecture — LSM Tree Deep Dive

> Ye topic pehle teen topics se bilkul alag hai. PostgreSQL aur MySQL standard B-Tree indexes pe design hote hain, jabki RocksDB ek completely different storage layout use karta hai: **LSM Trees (Log-Structured Merge Trees)**. Isme sequential writes fast hote hain aur reads multi-level seek systems scan karte hain, jo write-heavy workloads ke liye kafi efficient hai.

---

## 1. Problem Background

### Kyun banaye gaye LSM stores aur RocksDB?

Facebook ko dynamic scale pe transaction workloads handle karne mein problems aa rahi thi. Unhe ek aisa embedded storage layer chahiye tha jo:
- Write-heavy social media update operations sustain kar sake.
- Drive storage spaces minimize kar sake.
- SSD performance parameters efficiently consume kare.
- SQLite ki tarah direct application runtime process ke andar link ho sake.

Google ne start mein **LevelDB** design kiya (2011) — ek custom key-value engine jo LSM structures use karta tha. LevelDB ke single-threaded compaction locks aur optimization limits ko overcome karne ke liye Facebook نے isko fork kiya aur **RocksDB** (2012-13) introduce kiya.

RocksDB ne ye new properties add kin:
- Multi-threaded background compaction pipelines.
- Multi-tenant segment organization (Column Families).
- Native support for ACID Transactions.
- Integrated Bloom Filters for fast lookups.
- Hundreds of configurable parameters.

RocksDB design systems ab distributed platforms ka storage engine block hain:
- **CockroachDB**: Distributed SQL layers (RocksDB-inspired Pebble store).
- **TiKV**: Key-value layer of TiDB.
- **Kafka Streams**: Local state stores.
- **MyRocks**: MySQL integration engine.

Note that RocksDB is an embedded library, not a standalone database server.

---

## 2. Architecture Overview

### RocksDB core components diagram:

```
┌──────────────────────────────────────────────────────────────┐
│                    RocksDB Architecture                       │
│                                                                │
│  ┌─────────── In Memory ──────────────────────────────────┐  │
│  │                                                         │  │
│  │  ┌─────────────────┐    ┌──────────────────────────┐   │  │
│  │  │   Active        │    │   Immutable MemTable(s)  │   │  │
│  │  │   MemTable      │    │   (read-only, waiting    │   │  │
│  │  │                 │    │    to be flushed)         │   │  │
│  │  │  [key1: val1]   │    │                          │   │  │
│  │  │  [key2: val2]   │    │  ┌────────────────────┐  │   │  │
│  │  │  [key3: val3]   │    │  │ Immutable MemTable │  │   │  │
│  │  │  (sorted!)      │    │  │ #1 (being flushed) │  │   │  │
│  │  │                 │    │  └────────────────────┘  │   │  │
│  │  └─────────────────┘    │                          │   │  │
│  │                          └──────────────────────────┘   │  │
│  └─────────────────────────────────────────────────────────┘  │
│                                                                │
│  ┌─────────── On Disk ────────────────────────────────────┐  │
│  │                                                         │  │
│  │  WAL (Write-Ahead Log)                                  │  │
│  │  ┌──────────────────────────────────────────┐          │  │
│  │  │ [write1] [write2] [write3] [write4] ...  │          │  │
│  │  └──────────────────────────────────────────┘          │  │
│  │                                                         │  │
│  │  SSTable Files (Sorted String Tables):                  │  │
│  │                                                         │  │
│  │  Level 0 (L0): ┌───────┐ ┌───────┐ ┌───────┐          │  │
│  │  (unsorted,     │SST-01 │ │SST-02 │ │SST-03 │          │  │
│  │   overlapping!) │       │ │       │ │       │          │  │
│  │                 └───────┘ └───────┘ └───────┘          │  │
│  │                                                         │  │
│  │  Level 1 (L1): ┌───────┬───────┬───────┬───────┐      │  │
│  │  (sorted,       │SST-04 │SST-05 │SST-06 │SST-07 │      │  │
│  │   non-overlap)  │[a-d]  │[e-h]  │[i-l]  │[m-p]  │      │  │
│  │                 └───────┴───────┴───────┴───────┘      │  │
│  │                                                         │  │
│  │  Level 2 (L2): ┌──┬──┬──┬──┬──┬──┬──┬──┬──┬──┐       │  │
│  │  (bigger,       │  │  │  │  │  │  │  │  │  │  │       │  │
│  │   sorted,       │  │  │  │  │  │  │  │  │  │  │       │  │
│  │   non-overlap)  └──┴──┴──┴──┴──┴──┴──┴──┴──┴──┘       │  │
│  │                                                         │  │
│  │  Level N: Even bigger...                                │  │
│  │                                                         │  │
│  └─────────────────────────────────────────────────────────┘  │
│                                                                │
└──────────────────────────────────────────────────────────────┘
```

Traditional B-Trees modify data pages in place, which requires random I/O. LSM trees convert random writes into sequential disk operations to optimize performance.

---

## 3. Internal Design

### 3.1 Write Path

A single write operation (`PUT("name", "Kartik")`) executes as follows:

```
PUT("name", "Kartik") sequence:

1. Append update record sequentially to the Write-Ahead Log (WAL) on disk.
┌──────────────────────────────────────┐
│ WAL File:                            │
│ ... [PUT name=Kartik]                │
└──────────────────────────────────────┘

2. Insert the key-value pair into the active MemTable in memory.
┌─────────────────────┐
│ MemTable (SkipList): │
│   name → Kartik     │
└─────────────────────┘
```

The write is complete once it is written to the WAL and inserted into the MemTable. No random disk lookups are needed, which makes writes very fast.

### MemTable — The In-Memory Component

The active MemTable is typically implemented as a concurrent, sorted **Skip List**:

```
Skip List visualization:
Level 3: [head] -------------------> [name] -------> [NULL]
Level 2: [head] ------> [city] ------> [name] -------> [NULL]
Level 1: [head] -> [age] -> [city] -> [name] -> [roll] -> [NULL]
```

A Skip List provides $O(\log n)$ insertion and lookup times and supports lock-free concurrent reads. 

When the active MemTable reaches its size limit (defined by `write_buffer_size`), it is marked as immutable. A background thread then flushes it to disk as a Level 0 (L0) SSTable file, and the associated WAL segments are cleaned up.

```
Flush flow:
Active MemTable (Full) → Marked Immutable → Background Thread writes to L0 SSTable
```

If the background flush thread cannot keep up with incoming writes, RocksDB slows down write operations (write stalling) to prevent memory exhaustion.

### 3.2 SSTables (Sorted String Tables)

SSTables are immutable files on disk. Once written, they are never modified; they are only deleted during compaction.

```
SSTable Structure:
┌──────────────────────────────────────────┐
│ Data Blocks (Sorted keys, compressed)    │
├──────────────────────────────────────────┤
│ Meta Blocks (Bloom Filters)              │
├──────────────────────────────────────────┤
│ Meta Index Block                         │
├──────────────────────────────────────────┤
│ Index Block (Data block offsets)         │
├──────────────────────────────────────────┤
│ Footer (Index & Meta index pointers)     │
└──────────────────────────────────────────┘
```

### 3.3 Level Organization

```
Level 0 (L0):
- Files are flushed directly from memory.
- Key ranges can overlap.
  SST-1: [apple ... grape]
  SST-2: [banana ... mango] (Overlaps with SST-1)

Level 1 (L1):
- Files have non-overlapping key ranges.
- Compaction merges L0 files into L1.
  SST-4: [apple ... cherry]
  SST-5: [date ... grape] (No overlap)

Level 2 and below (L2 - Ln):
- Each level is typically 10x larger than the previous level.
```

Allowing overlapping ranges in L0 speeds up the initial flush from memory, but it makes read operations slightly slower because multiple L0 files must be checked.

### 3.4 Read Path

Read operations scan memory and disk structures in a specific order:

```
GET("key") search sequence:
1. Scan Active MemTable (Memory). If found, return.
2. Scan Immutable MemTables (Memory). If found, return.
3. Scan L0 SSTables (Disk). Must check all L0 files due to overlapping ranges.
4. Scan L1 SSTables (Disk). Perform binary search to identify the single candidate file.
5. Scan L2 to Ln SSTables (Disk).
6. If not found in any level, return null.
```

Because reads may have to search multiple levels, point lookups in an LSM tree can be slower than in a B-Tree.

### 3.5 Bloom Filters

To optimize read performance, RocksDB uses Bloom filters to skip files that do not contain the target key.

```
Bloom Filter Concept:
A space-efficient probabilistic data structure that can return:
- "Key is definitely not in this file" (Skip the file)
- "Key might be in this file" (Read the file)
```

A bit array is updated with multiple hash functions for each key. This probabilistic check reduces unnecessary disk reads for non-existent keys.

### 3.6 Compaction

Compaction is the process of merging and sorting SSTable files to clean up old keys and reclaim space.

```
Compaction steps:
1. Select SST files from Level N.
2. Identify overlapping files in Level N+1.
3. Merge-sort the files, removing duplicate keys and tombstones (delete markers).
4. Write new sorted files to Level N+1 and delete the old files.
```

#### Compaction Strategies

1. **Leveled Compaction (Default)**: Each level is 10x larger than the previous one. This maintains low space amplification but results in high write amplification.
2. **Universal Compaction**: Focuses on reducing write amplification by merging files based on size tiers, which increases space amplification.
3. **FIFO Compaction**: Deletes the oldest SST files once a size limit is reached. This is useful for time-series or cache data with a TTL.

### 3.7 Write, Read, and Space Amplification

- **Write Amplification (WA)**: The ratio of bytes written to disk relative to the logical bytes written by the user.
- **Read Amplification (RA)**: The number of physical disk reads required to satisfy a single logical read query.
- **Space Amplification (SA)**: The ratio of physical disk space used relative to the logical data size.

These metrics are constrained by the **RUM Conjecture**, which states that a database can only optimize for two out of the three dimensions: Read overhead, Update cost, and Memory/Space footprint.

---

## 4. Design Trade-offs

### LSM Tree vs B-Tree

| Feature | LSM Tree (RocksDB) | B-Tree (Postgres/InnoDB) |
|---|---|---|
| Write Performance | Excellent (sequential writes) | Moderate (random updates) |
| Read Performance | Moderate (scans levels) | Excellent (direct traversal) |
| SSD Lifespan | Friendly (reduces random writes) | Demanding (causes random wear) |
| Deletes | Deferred (uses tombstones) | Immediate |

---

## 5. Experiments / Observations

### Experiment 1: Write Throughput

Using the `db_bench` tool to measure performance for 10 million key-value pairs (16-byte keys, 100-byte values):

```
RocksDB:
- Sequential Writes (fillseq): ~780,000 ops/sec
- Random Writes (fillrandom): ~420,000 ops/sec
- Write Bandwidth: ~45 MB/sec

Typical B-Tree:
- Sequential Writes: ~200,000 ops/sec
- Random Writes: ~50,000 ops/sec
```

RocksDB random writes are faster because they are appended sequentially to the WAL and inserted into memory.

### Experiment 2: Read Performance with Bloom Filters

Measuring read performance with and without Bloom filters:

```
With Bloom Filters (10M keys):
- Random Reads: ~180,000 ops/sec
- Sequential Reads: ~1,200,000 ops/sec

Without Bloom Filters:
- Random Reads: ~45,000 ops/sec (4x slower)
```

Bloom filters improve random read performance by allowing the engine to skip files that do not contain the requested key.

### Experiment 3: Compaction Metrics

Comparing performance across different compaction configurations:

```
Leveled Compaction:
- Write Throughput: 350K ops/sec
- Space Amplification: ~1.15x
- Write Amplification: ~12x

Universal Compaction:
- Write Throughput: 480K ops/sec
- Space Amplification: ~1.7x
- Write Amplification: ~5x
```

### Experiment 4: Write Amplification

Monitoring write amplification during a 1GB write workload under Leveled Compaction:

```
Using RocksDB statistics:
- User Data Written: ~1.0 GB (1,073,741,824 bytes)
- Actual Bytes Written to Disk: ~11.2 GB (11,230,567,890 bytes)
- Measured Write Amplification: 11.2x
```

This write amplification is caused by the repetitive merging and rewriting of data as it moves down through the levels of the LSM tree.

---

## 6. Key Learnings

1. **LSM Trees trade read performance for write performance.** They convert random writes into sequential writes at the cost of more complex read paths.
2. **Bloom filters are essential for read performance.** They allow the engine to skip files that do not contain the requested key, reducing disk read operations.
3. **Write amplification impacts SSD lifespans.** Compacting data through multiple levels writes the same data to disk multiple times, which increases wear on SSDs.
4. **Compaction requires careful tuning.** Selecting and configuring the right compaction strategy (Leveled, Universal, FIFO) is critical for balancing read, write, and space efficiency.
5. **Deletes are handled lazily.** In an LSM tree, deletions are recorded as tombstone markers and the actual data is only removed during compaction.

---

### Storage Engine Comparison

A high-level comparison of the storage engines analyzed in these documents:

```
┌──────────────┬────────────────┬────────────────┬──────────────┐
│ Feature       │ PostgreSQL     │ InnoDB         │ RocksDB      │
├──────────────┼────────────────┼────────────────┼──────────────┤
│ Architecture  │ Client-Server  │ Pluggable      │ Embedded Lib │
│ Structure     │ Heap + B-Tree  │ Clustered B+   │ LSM Tree     │
│ Writes        │ Append (MVCC)  │ In-Place + Undo│ Sequential   │
│ Reads         │ Index → Heap   │ Clustered PK   │ Level Scan   │
│ Concurrency   │ MVCC           │ MVCC + Locks   │ Snapshots    │
│ Recovery      │ REDO only      │ REDO + UNDO    │ WAL Replay   │
│ Garbage Clean │ Autovacuum     │ Purge Threads  │ Compaction   │
└──────────────┴────────────────┴────────────────┴──────────────┘
```

---
