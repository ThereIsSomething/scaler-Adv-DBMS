# SQLite Database Internal Structure Inspection using `xxd`
BY NITISH KUMAR BHAMBU (224BCS10589)
## Aim

To inspect and understand the internal binary structure of an SQLite database file using hexadecimal dumping tools and observe how SQLite stores schema information, pages, B-Tree structures, and records internally.

---

## Tools Used

- SQLite3 CLI
- `xxd`
- Linux Terminal
- `students.db`

---

# Step 1 — Creating / Opening Database

Command used:

```bash
sqlite3 students.db
```

Listing tables:

```sql
.tables
```

Output:

```txt
students
```

---

# Step 2 — Dumping Database in Hexadecimal Form

Command used:

```bash
xxd -g 1 students.db > dump.txt
```

### Meaning of command

| Command Part | Description |
|---|---|
| `xxd` | Generates hexadecimal dump |
| `-g 1` | Groups bytes individually |
| `students.db` | SQLite database file |
| `>` | Redirects output |
| `dump.txt` | Stores dump into text file |

---

# Step 3 — Inspecting SQLite Header

Beginning of dump:

```txt
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00
```

ASCII interpretation:

```txt
SQLite format 3
```

This confirms that the file is a valid SQLite3 database.

---

# Step 4 — Determining Page Size

At offset `0x10`:

```txt
00000010: 10 00
```

Hexadecimal value:

```txt
0x1000 = 4096
```

Hence,

```txt
Page Size = 4096 bytes
```

SQLite divides the entire database into fixed-size pages.

---

# Step 5 — Finding Total Number of Pages

At offset `0x1C`:

```txt
00 00 00 04
```

This means:

```txt
Total Pages = 4
```

Total database size:

```txt
4 × 4096 = 16384 bytes
```

---

# Step 6 — Beginning of First B-Tree Page

SQLite database header occupies first 100 bytes.

After offset `0x64`, B-Tree structure begins.

Observed bytes:

```txt
0d 0f f8 00 03
```

---

# Step 7 — Decoding B-Tree Page Header

## Page Type

First byte:

```txt
0d
```

Meaning:

```txt
Table Leaf Page
```

SQLite page type values:

| Hex | Meaning |
|---|---|
| `0D` | Table Leaf |
| `05` | Table Interior |
| `0A` | Index Leaf |
| `02` | Index Interior |

---

## Number of Cells

Bytes:

```txt
00 03
```

Meaning:

```txt
3 cells present in page
```

These cells store records inside the B-Tree page.

---

# Step 8 — Cell Pointer Array

Observed values:

```txt
0e 77
0e 77
0f c7
```

These represent pointers to actual cell contents stored later in the page.

SQLite stores:

- Header at top
- Pointer array below header
- Actual records at bottom of page

---

# Step 9 — Free Space Observation

Large regions of:

```txt
00 00 00 00
```

were observed.

This represents unused free space inside the page.

SQLite allocates full pages even if only small data exists.

---

# Step 10 — Schema Information Stored Internally

At offset around `0xE70`, readable text appeared:

```txt
table
students
CREATE TABLE students
```

This indicates that SQLite stores schema information as actual records inside an internal table called:

```sql
sqlite_schema
```

---

# Step 11 — Observed CREATE TABLE Statement

The following SQL statement was visible directly in hexadecimal dump:

```sql
CREATE TABLE students (
    student_id SERIAL PRIMARY KEY,
    first_name VARCHAR(100) NOT NULL,
    last_name VARCHAR(100) NOT NULL,
    age INT,
    email VARCHAR(255) UNIQUE,
    course VARCHAR(100),
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
)
```

This shows that SQLite stores SQL schema definitions directly inside the database file.

---

# Step 12 — Root Page Information

Inside schema record:

```txt
students 02
```

Meaning:

```txt
Root page of students table = Page 2
```

---

# Step 13 — Inspecting Page 2

At offset:

```txt
00001000
```

which equals:

```txt
4096 decimal
```

New B-Tree page begins.

Observed bytes:

```txt
0d 00 00 00 02
```

Interpretation:

| Value | Meaning |
|---|---|
| `0d` | Table leaf page |
| `0002` | 2 cells |

This corresponds to the 2 inserted student records.

---

# Overall Database Structure

Current database organization:

```txt
Page 1  -> sqlite_schema table
Page 2  -> students table data
Page 3  -> auto-generated index
Page 4  -> additional index/internal structure
```

---

# Observations

1. SQLite stores data using fixed-size pages.
2. Tables are internally represented as B-Trees.
3. Schema information is stored as records.
4. SQL statements themselves are physically stored in database file.
5. SQLite uses page headers and cell pointer arrays for managing records.
6. Empty space inside pages remains reserved for future insertions.

---

# Conclusion

The internal structure of SQLite database was successfully inspected using hexadecimal dumping.

The experiment demonstrated:

- SQLite file header structure
- Page organization
- B-Tree page format
- Cell pointer arrays
- Internal schema storage
- Record storage behavior

This inspection helped in understanding how relational database systems physically organize and manage data internally instead of only viewing them through SQL queries.

---

## Commands Used During Experiment

```bash
sqlite3 students.db
.tables
xxd -g 1 students.db > dump.txt
less dump.txt
```

---

## Reference Dump Snippets

```txt
00000000: 53 51 4c 69 74 65 20 66 6f 72 6d 61 74 20 33 00
00000010: 10 00
00000064: 0d 0f f8 00 03
00000e70: table students CREATE TABLE students
00001000: 0d 00 00 00 02
```

