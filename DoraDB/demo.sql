-- ============================================================
-- DoraDB Live Demonstration Script
-- Run: ./doradb   then   \i demo.sql
-- ============================================================

-- 1. CREATE TABLE — shows parser + catalog + heap page allocation
CREATE TABLE students (id INT, name VARCHAR(50), active BOOL, PRIMARY KEY(id));
CREATE TABLE courses (cid INT, student_id INT, course VARCHAR(30), PRIMARY KEY(cid));

-- 2. INSERT — shows serialization + slotted page + B+Tree index insert
INSERT INTO students VALUES (1, 'Alice', true);
INSERT INTO students VALUES (2, 'Bob', false);
INSERT INTO students VALUES (3, 'Charlie', true);
INSERT INTO students VALUES (4, 'Diana', false);
INSERT INTO students VALUES (5, 'Eve', true);

INSERT INTO courses VALUES (1, 1, 'DBMS');
INSERT INTO courses VALUES (2, 2, 'OS');
INSERT INTO courses VALUES (3, 1, 'Networks');
INSERT INTO courses VALUES (4, 3, 'Algorithms');

-- 3. SELECT * — full table scan (SeqScan via HeapFile linked-list traversal)
SELECT * FROM students;

-- 4. SELECT with WHERE on PK — optimizer decides SeqScan vs IndexScan
SELECT * FROM students WHERE id = 3;

-- 5. Range query — optimizer evaluates selectivity
SELECT * FROM students WHERE id > 1 AND id <= 4;

-- 6. Non-PK filter — forces SeqScan + FilterNode
SELECT * FROM students WHERE active = true;

-- 7. Projection — only specific columns
SELECT name FROM students WHERE id = 1;

-- 8. JOIN — NestedLoopJoin with optimizer outer-table selection
SELECT * FROM students JOIN courses ON students.id = courses.student_id;

-- 9. UPDATE — modifies row in-place on slotted page
UPDATE students SET name = 'Alicia' WHERE id = 1;
SELECT * FROM students WHERE id = 1;

-- 10. DELETE — marks slot as tombstone, removes from B+Tree
DELETE FROM students WHERE id = 5;
SELECT * FROM students;

-- 11. Error handling — graceful message for missing table
SELECT * FROM nonexistent;
