// sql_engine.h  —  ADBMS Lab 5  |  24BCS10213  Jatin Chulet
//
// Header for a lightweight in-memory SQL SELECT processor built around
// Dijkstra's shunting-yard algorithm.  The processing stages are:
//
//     raw SQL  --tokenize()-->   token list
//              --parse_select()-> QueryPlan   (WHERE stored as RPN)
//              --execute()------> result Table (filter → project → sort → cap)
//
// The WHERE expression is converted from infix to postfix (RPN) once by
// shunting_yard(), then each row is tested with eval_rpn() using a value stack.
//
// Cell types: a 64-bit integer OR a UTF-8 string (std::variant).
// Supported WHERE ops: =  !=  <  <=  >  >=  AND  OR  NOT  and parentheses.

#ifndef LAB7_JATIN_SQL_ENGINE_H
#define LAB7_JATIN_SQL_ENGINE_H

#include <iosfwd>
#include <string>
#include <variant>
#include <vector>

namespace queryengine {

// ---------------------------------------------------------------------------
// Core data types
// ---------------------------------------------------------------------------

// A cell holds either a signed 64-bit integer (index 0) or a string (index 1).
using CellVal = std::variant<long long, std::string>;

// One record in a table — cells are ordered to match the parent Table's header.
struct Record {
    std::vector<CellVal> fields;
};

// An in-memory relation: a name, an ordered list of column headers, and rows.
struct Table {
    std::string              name;
    std::vector<std::string> header;   // column names in declaration order
    std::vector<Record>      data;

    // Returns the zero-based position of column col, or -1 if not found.
    int col_pos(const std::string& col) const;
};

// ---------------------------------------------------------------------------
// Tokenizer
// ---------------------------------------------------------------------------

enum class TokKind {
    Ident, IntLit, StrLit,                       // value-carrying tokens
    Op,                                           // comparison / logical operators
    LParen, RParen, Comma, Star,
    KwSelect, KwFrom, KwWhere,
    KwOrder, KwBy, KwAsc, KwDesc, KwLimit,
    Eof
};

struct Token {
    TokKind     kind;
    std::string raw;       // original text, operator symbol, or unquoted string value
    long long   num = 0;   // populated when kind == TokKind::IntLit
};

// Break a SQL string into a flat token list (terminated by Eof).
std::vector<Token> tokenize(const std::string& sql);

// ---------------------------------------------------------------------------
// Shunting-yard  (infix → postfix / RPN)
// ---------------------------------------------------------------------------

// Re-order a WHERE token sequence from infix to RPN using the shunting-yard
// algorithm; honours operator precedence and associativity.
std::vector<Token> shunting_yard(const std::vector<Token>& infix_tokens);

// Evaluate a compiled RPN predicate for a single record; returns true/false.
bool eval_rpn(const std::vector<Token>& rpn_expr,
              const Table& tbl_schema, const Record& rec);

// Flatten an RPN token list to a printable string (useful for debug output).
std::string rpn_to_string(const std::vector<Token>& rpn_expr);

// ---------------------------------------------------------------------------
// SELECT statement representation + execution
// ---------------------------------------------------------------------------

struct QueryPlan {
    std::vector<std::string> select_cols;    // columns to keep; empty = keep all
    std::string              src_table;
    std::vector<Token>       where_rpn;      // compiled predicate; empty = no filter
    std::string              sort_col;       // "" = no ORDER BY
    bool                     sort_desc = false;
    long long                row_cap   = -1; // -1 = no LIMIT
};

// Parse a SELECT … FROM … [WHERE …] [ORDER BY … [ASC|DESC]] [LIMIT n] string.
QueryPlan parse_select(const std::string& sql);

// Apply the plan to a source table and return the filtered/projected result.
Table execute(const QueryPlan& plan, const Table& source);

// Display a table in a fixed-width grid to an output stream.
void print_table(const Table& tbl, std::ostream& os);

}  // namespace queryengine

#endif  // LAB7_JATIN_SQL_ENGINE_H
