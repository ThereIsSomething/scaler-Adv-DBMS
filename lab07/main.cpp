// main.cpp  —  ADBMS Lab 5  |  24BCS10213  Jatin Chulet
//
// Demonstration driver for the queryengine SQL SELECT processor.
//
// Part 1 — Shunting-yard: converts sample infix WHERE expressions to RPN
//           and prints both forms side-by-side.
//
// Part 2 — Full pipeline: runs SELECT queries against an in-memory
//           'employees' table (tokenize → parse → execute) and validates
//           the results with assertions.

#include "sql_engine.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace queryengine;

namespace {

// Print a prominent section header.
void section(const std::string& title) {
    std::cout << "\n=== " << title << " ===\n";
}

// Assertion helper — exits on failure.
void verify(bool condition, const std::string& msg) {
    if (!condition) {
        std::cerr << "ASSERTION FAILED: " << msg << "\n";
        std::exit(EXIT_FAILURE);
    }
}

// Build the shared demo dataset once.
Table build_employees() {
    Table emp;
    emp.name   = "employees";
    emp.header = {"id", "name", "dept", "age", "salary"};

    auto add_row = [&](long long id,
                       const std::string& nm,
                       const std::string& dept,
                       long long          age,
                       long long          sal) {
        emp.data.push_back(Record{{
            CellVal{id}, CellVal{nm}, CellVal{dept}, CellVal{age}, CellVal{sal}
        }});
    };

    add_row(1, "Aarav",  "Engineering", 29,  95000);
    add_row(2, "Diya",   "Sales",       24,  55000);
    add_row(3, "Rohan",  "Engineering", 35, 120000);
    add_row(4, "Meera",  "Marketing",   41,  88000);
    add_row(5, "Kabir",  "Sales",       28,  61000);
    add_row(6, "Ishita", "Engineering", 31,  99000);
    add_row(7, "Vivaan", "Marketing",   23,  47000);
    add_row(8, "Anaya",  "Engineering", 38, 134000);

    return emp;
}

// Shorthand for result row count.
std::size_t row_count(const Table& t) { return t.data.size(); }

}  // anonymous namespace

int main() {

    // -----------------------------------------------------------------------
    section("Part 1)  Shunting-Yard — infix WHERE  ->  postfix / RPN");
    // -----------------------------------------------------------------------

    const std::vector<std::string> sample_exprs = {
        "age > 25 AND (dept = 'Sales' OR salary >= 100000)",
        "NOT age < 30 AND salary > 90000",
        "id = 1 OR id = 3 OR id = 8",
    };

    for (const std::string& expr : sample_exprs) {
        std::vector<Token> rpn = shunting_yard(tokenize(expr));
        std::cout << "  infix : " << expr                  << "\n";
        std::cout << "  RPN   : " << rpn_to_string(rpn)    << "\n\n";
    }

    Table emp_table = build_employees();

    // -----------------------------------------------------------------------
    section("Part 2a)  SELECT * FROM employees");
    // -----------------------------------------------------------------------
    {
        Table result = execute(parse_select("SELECT * FROM employees"), emp_table);
        print_table(result, std::cout);
        verify(row_count(result) == 8, "SELECT * should return all 8 rows");
    }

    // -----------------------------------------------------------------------
    section("Part 2b)  WHERE with AND / OR / parentheses  +  column projection");
    // -----------------------------------------------------------------------
    {
        const std::string query =
            "SELECT name, dept, salary FROM employees "
            "WHERE age > 25 AND (dept = 'Sales' OR salary >= 100000) "
            "ORDER BY salary DESC";

        std::cout << "SQL: " << query << "\n";
        Table result = execute(parse_select(query), emp_table);
        print_table(result, std::cout);

        // Expected matches: Rohan (Eng, 120000), Anaya (Eng, 134000), Kabir (Sales, 61000, age 28)
        // Sorted DESC by salary: Anaya → Rohan → Kabir
        verify(row_count(result) == 3,
               "exactly 3 rows should satisfy the predicate");
        verify(std::get<std::string>(result.data[0].fields[0]) == "Anaya",
               "highest-paid record should come first");
        verify(std::get<std::string>(result.data[2].fields[0]) == "Kabir",
               "Kabir should be last (lowest salary among the three)");
    }

    // -----------------------------------------------------------------------
    section("Part 2c)  NOT, string comparison, ORDER BY ASC, and LIMIT");
    // -----------------------------------------------------------------------
    {
        const std::string query =
            "SELECT name, age FROM employees "
            "WHERE NOT dept = 'Engineering' "
            "ORDER BY age ASC LIMIT 2";

        std::cout << "SQL: " << query << "\n";
        Table result = execute(parse_select(query), emp_table);
        print_table(result, std::cout);

        // Non-engineering staff: Diya(24), Meera(41), Kabir(28), Vivaan(23)
        // Sorted by age ASC, top 2 → Vivaan(23), Diya(24)
        verify(row_count(result) == 2,
               "LIMIT 2 should cap the result at two rows");
        verify(std::get<std::string>(result.data[0].fields[0]) == "Vivaan",
               "Vivaan is the youngest non-engineering employee");
        verify(std::get<long long>(result.data[1].fields[1]) == 24,
               "second row should have age 24 (Diya)");
    }

    // -----------------------------------------------------------------------
    section("Part 2d)  Direct per-record predicate evaluation via eval_rpn");
    // -----------------------------------------------------------------------
    {
        std::vector<Token> predicate =
            shunting_yard(tokenize("dept = 'Engineering' AND salary >= 100000"));

        std::cout << "predicate RPN: " << rpn_to_string(predicate) << "\n";

        std::size_t match_count = 0;
        for (const Record& rec : emp_table.data) {
            if (eval_rpn(predicate, emp_table, rec)) {
                ++match_count;
                std::cout << "  match: "
                          << std::get<std::string>(rec.fields[1])
                          << "\n";
            }
        }

        // Only Rohan (120000) and Anaya (134000) qualify.
        verify(match_count == 2, "Rohan and Anaya are the only matches");
    }

    std::cout << "\nAll checks passed — engine working correctly.\n";
    return EXIT_SUCCESS;
}
