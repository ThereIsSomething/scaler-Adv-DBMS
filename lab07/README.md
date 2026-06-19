# ADBMS Lab 5 – Shunting-Yard Based SQL Query Engine (C++17)

**Name:** Jatin Chulet
**Roll Number:** 24BC10213
**Course:** Advanced Database Management Systems (ADBMS)
**Institution:** Scaler School of Technology

---

## Overview

This project implements a miniature SQL query engine in C++17 using Dijkstra's Shunting-Yard Algorithm. The objective of the lab is to demonstrate how relational database systems parse, process, and execute SQL queries internally.

The implementation consists of two major components:

1. **Shunting-Yard Algorithm**

    * Converts infix boolean expressions from SQL WHERE clauses into postfix (Reverse Polish Notation).
    * Handles operator precedence and parentheses efficiently.

2. **SQL Query Execution Engine**

    * Tokenizes SQL statements.
    * Parses SELECT queries.
    * Executes filtering, projection, sorting, and limiting operations on an in-memory table.

The WHERE clause is converted into postfix notation only once during query parsing and is then evaluated efficiently for each row during execution.

---

## Project Structure

```text
Lab-7/
├── main.cpp
├── sql_engine.cpp
├── sql_engine.h
├── Makefile
└── README.md
```

### File Description

| File           | Description                               |
| -------------- | ----------------------------------------- |
| main.cpp       | Driver program and test cases             |
| sql_engine.h   | Data structures and function declarations |
| sql_engine.cpp | SQL engine implementation                 |
| Makefile       | Build automation                          |
| README.md      | Project documentation                     |

---

## Features Implemented

### SQL Operations

Supported query format:

```sql
SELECT column_list
FROM table_name
[WHERE condition]
[ORDER BY column ASC|DESC]
[LIMIT n]
```

### Supported Operators

```sql
=
!=
<
<=
>
>=
AND
OR
NOT
```

### Supported Data Types

* Integer (`long long`)
* String (`std::string`)

---

## Query Processing Pipeline

```text
SQL Query
    │
    ▼
Tokenizer
    │
    ▼
Parser
    │
    ▼
Shunting-Yard Algorithm
(Infix → Postfix)
    │
    ▼
Execution Engine
    │
    ├── WHERE Filtering
    ├── Projection
    ├── ORDER BY
    └── LIMIT
    │
    ▼
Result Table
```

---

## Shunting-Yard Algorithm

The Shunting-Yard Algorithm converts infix expressions into postfix notation so that expressions can be evaluated using a simple stack-based approach.

Example:

```text
Infix:
age > 25 AND (dept = 'Sales' OR salary >= 100000)

Postfix:
age 25 > dept 'Sales' = salary 100000 >= OR AND
```

### Operator Precedence

| Priority | Operators           |
| -------- | ------------------- |
| 4        | =, !=, <, <=, >, >= |
| 3        | NOT                 |
| 2        | AND                 |
| 1        | OR                  |

---

## Execution Process

The query engine executes statements in the following order:

1. WHERE filtering
2. Projection (column selection)
3. ORDER BY sorting
4. LIMIT restriction

This closely follows the logical execution order used by real database systems.

---

## Sample Query

```sql
SELECT name, dept, salary
FROM employees
WHERE age > 25
AND (dept = 'Sales' OR salary >= 100000)
ORDER BY salary DESC;
```

---

## Demonstration Cases

### Case 1

Conversion of infix expressions to postfix notation using the Shunting-Yard Algorithm.

### Case 2

Display all records:

```sql
SELECT * FROM employees;
```

### Case 3

Filtering, projection, and sorting:

```sql
SELECT name, dept, salary
FROM employees
WHERE age > 25
AND (dept = 'Sales' OR salary >= 100000)
ORDER BY salary DESC;
```

### Case 4

Use of NOT operator and LIMIT:

```sql
SELECT name, age
FROM employees
WHERE NOT dept = 'Engineering'
ORDER BY age ASC
LIMIT 2;
```

---

## Complexity Analysis

| Operation                | Complexity   |
| ------------------------ | ------------ |
| Tokenization             | O(L)         |
| Parsing                  | O(T)         |
| Shunting-Yard Conversion | O(T)         |
| Predicate Evaluation     | O(T) per row |
| Filtering                | O(N × T)     |
| Sorting                  | O(N log N)   |

Where:

* **L** = Length of SQL query
* **T** = Number of tokens
* **N** = Number of rows

---

## Build Instructions

Compile and run using:

```bash
make run
```

Or manually:

```bash
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic main.cpp sql_engine.cpp -o adbms_lab7
./adbms_lab7
```

---

## Learning Outcomes

Through this lab, the following concepts were explored:

* SQL query parsing
* Tokenization techniques
* Shunting-Yard Algorithm
* Postfix expression evaluation
* Stack-based computation
* Query execution pipelines
* Database filtering and sorting mechanisms

---

## Conclusion

This project demonstrates the core concepts involved in SQL query processing by implementing a lightweight query engine from scratch. The combination of tokenization, parsing, postfix conversion, expression evaluation, filtering, sorting, and limiting provides a practical understanding of how relational database systems execute queries internally.
