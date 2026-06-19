// sql_engine.cpp  —  ADBMS Lab 5  |  24BCS10213  Jatin Chulet
//
// Full implementation of the queryengine SQL SELECT processor.
// Refer to sql_engine.h for the public interface.

#include "sql_engine.h"

#include <algorithm>
#include <cctype>
#include <ostream>
#include <stack>
#include <stdexcept>
#include <string>

namespace queryengine {

namespace {

// Convert every character in a string to uppercase.
std::string to_upper(std::string s) {
    for (char& ch : s)
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    return s;
}

// Predicates for identifier scanning.
bool starts_ident(char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) || ch == '_';
}
bool cont_ident(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
}

// Operator metadata used by the shunting-yard pass.
// 'prec' = precedence (higher binds tighter); 'unary' flags prefix operators;
// 'rassoc' flags right-associative operators.
struct OpMeta { int prec; bool unary; bool rassoc; };

// Return the metadata for a given operator symbol; {0,false,false} if unknown.
OpMeta get_op_meta(const std::string& sym) {
    // Comparison operators all share the same (highest) precedence level.
    if (sym == "=" || sym == "!=" ||
        sym == "<" || sym == "<=" ||
        sym == ">" || sym == ">=")   return {4, false, false};

    if (sym == "NOT")                return {3, true,  true };
    if (sym == "AND")                return {2, false, false};
    if (sym == "OR")                 return {1, false, false};

    return {0, false, false};   // not a recognised operator
}

// Interpret a CellVal as a boolean (non-zero int or non-empty string = true).
bool to_bool(const CellVal& val) {
    if (std::holds_alternative<long long>(val))
        return std::get<long long>(val) != 0;
    return !std::get<std::string>(val).empty();
}

// Three-way comparison: returns -1 / 0 / 1.
// 'valid' is set to false when the two values have incompatible types.
int cmp_values(const CellVal& left, const CellVal& right, bool& valid) {
    valid = true;

    // Both integers — numeric comparison.
    if (std::holds_alternative<long long>(left) &&
        std::holds_alternative<long long>(right)) {
        long long lv = std::get<long long>(left);
        long long rv = std::get<long long>(right);
        return (lv < rv) ? -1 : (lv > rv) ? 1 : 0;
    }

    // Both strings — lexicographic comparison.
    if (std::holds_alternative<std::string>(left) &&
        std::holds_alternative<std::string>(right)) {
        const std::string& ls = std::get<std::string>(left);
        const std::string& rs = std::get<std::string>(right);
        return (ls < rs) ? -1 : (ls > rs) ? 1 : 0;
    }

    // Mixed types — unorderable.
    valid = false;
    return 0;
}

// Materialise a token to a concrete CellVal given the current record context.
// Identifiers are resolved to the matching column value.
CellVal resolve_token(const Token& tok,
                      const Table& tbl_schema,
                      const Record& rec) {
    switch (tok.kind) {
        case TokKind::IntLit:
            return CellVal{tok.num};

        case TokKind::StrLit:
            return CellVal{tok.raw};

        case TokKind::Ident: {
            int pos = tbl_schema.col_pos(tok.raw);
            if (pos < 0)
                throw std::runtime_error("unrecognised column: " + tok.raw);
            return rec.fields[static_cast<std::size_t>(pos)];
        }

        default:
            throw std::runtime_error("token cannot be used as an operand: " + tok.raw);
    }
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Table — column lookup
// ---------------------------------------------------------------------------

int Table::col_pos(const std::string& col) const {
    for (std::size_t i = 0; i < header.size(); ++i)
        if (header[i] == col) return static_cast<int>(i);
    return -1;
}

// ---------------------------------------------------------------------------
// Tokenizer
// ---------------------------------------------------------------------------

std::vector<Token> tokenize(const std::string& sql) {
    std::vector<Token> tokens;
    const int len = static_cast<int>(sql.size());
    int pos = 0;

    while (pos < len) {
        char ch = sql[pos];

        // Skip whitespace.
        if (std::isspace(static_cast<unsigned char>(ch))) { ++pos; continue; }

        // Identifier or keyword.
        if (starts_ident(ch)) {
            int start = pos;
            while (pos < len && cont_ident(sql[pos])) ++pos;

            std::string word = sql.substr(static_cast<std::size_t>(start),
                                          static_cast<std::size_t>(pos - start));
            std::string upper = to_upper(word);

            if      (upper == "SELECT") tokens.push_back({TokKind::KwSelect, upper});
            else if (upper == "FROM")   tokens.push_back({TokKind::KwFrom,   upper});
            else if (upper == "WHERE")  tokens.push_back({TokKind::KwWhere,  upper});
            else if (upper == "ORDER")  tokens.push_back({TokKind::KwOrder,  upper});
            else if (upper == "BY")     tokens.push_back({TokKind::KwBy,     upper});
            else if (upper == "ASC")    tokens.push_back({TokKind::KwAsc,    upper});
            else if (upper == "DESC")   tokens.push_back({TokKind::KwDesc,   upper});
            else if (upper == "LIMIT")  tokens.push_back({TokKind::KwLimit,  upper});
            else if (upper == "AND" || upper == "OR" || upper == "NOT")
                                        tokens.push_back({TokKind::Op, upper});
            else                        tokens.push_back({TokKind::Ident, word});
            continue;
        }

        // Integer literal.
        if (std::isdigit(static_cast<unsigned char>(ch))) {
            int start = pos;
            while (pos < len && std::isdigit(static_cast<unsigned char>(sql[pos]))) ++pos;
            Token t{TokKind::IntLit,
                    sql.substr(static_cast<std::size_t>(start),
                               static_cast<std::size_t>(pos - start))};
            t.num = std::stoll(t.raw);
            tokens.push_back(t);
            continue;
        }

        // Single-quoted string literal; '' inside the literal becomes a lone '.
        if (ch == '\'') {
            std::string buf;
            ++pos;
            while (pos < len) {
                if (sql[pos] == '\'') {
                    // Escaped quote: '' -> '
                    if (pos + 1 < len && sql[pos + 1] == '\'') {
                        buf += '\'';
                        pos += 2;
                        continue;
                    }
                    ++pos;
                    break;
                }
                buf += sql[pos++];
            }
            tokens.push_back({TokKind::StrLit, buf});
            continue;
        }

        // Single-character punctuation and multi-character operators.
        switch (ch) {
            case '(': tokens.push_back({TokKind::LParen, "("}); ++pos; break;
            case ')': tokens.push_back({TokKind::RParen, ")"}); ++pos; break;
            case ',': tokens.push_back({TokKind::Comma,  ","}); ++pos; break;
            case '*': tokens.push_back({TokKind::Star,   "*"}); ++pos; break;
            case '=': tokens.push_back({TokKind::Op,     "="}); ++pos; break;

            case '!':
                if (pos + 1 < len && sql[pos + 1] == '=') {
                    tokens.push_back({TokKind::Op, "!="});
                    pos += 2;
                } else {
                    throw std::runtime_error("unexpected character '!'");
                }
                break;

            case '<':
                if (pos + 1 < len && sql[pos + 1] == '=') {
                    tokens.push_back({TokKind::Op, "<="});
                    pos += 2;
                } else {
                    tokens.push_back({TokKind::Op, "<"});
                    ++pos;
                }
                break;

            case '>':
                if (pos + 1 < len && sql[pos + 1] == '=') {
                    tokens.push_back({TokKind::Op, ">="});
                    pos += 2;
                } else {
                    tokens.push_back({TokKind::Op, ">"});
                    ++pos;
                }
                break;

            default:
                throw std::runtime_error(std::string("unexpected character: ") + ch);
        }
    }

    tokens.push_back({TokKind::Eof, ""});
    return tokens;
}

// ---------------------------------------------------------------------------
// Shunting-yard  +  RPN evaluation
// ---------------------------------------------------------------------------

std::vector<Token> shunting_yard(const std::vector<Token>& infix_tokens) {
    std::vector<Token> out_queue;  // the final RPN output
    std::stack<Token>  op_stack;   // temporary operator/paren store

    for (const Token& tok : infix_tokens) {
        switch (tok.kind) {
            // Operands go straight to output.
            case TokKind::Ident:
            case TokKind::IntLit:
            case TokKind::StrLit:
                out_queue.push_back(tok);
                break;

            case TokKind::LParen:
                op_stack.push(tok);
                break;

            // Pop until we find the matching '(', then discard it.
            case TokKind::RParen:
                while (!op_stack.empty() &&
                       op_stack.top().kind != TokKind::LParen) {
                    out_queue.push_back(op_stack.top());
                    op_stack.pop();
                }
                if (op_stack.empty())
                    throw std::runtime_error("unmatched closing parenthesis");
                op_stack.pop();   // drop the '('
                break;

            // Operator: pop higher- (or equal- for left-assoc) precedence ops first.
            case TokKind::Op: {
                OpMeta cur = get_op_meta(tok.raw);
                while (!op_stack.empty() &&
                       op_stack.top().kind == TokKind::Op) {
                    OpMeta top = get_op_meta(op_stack.top().raw);
                    bool do_pop = (top.prec > cur.prec) ||
                                  (top.prec == cur.prec && !cur.rassoc);
                    if (!do_pop) break;
                    out_queue.push_back(op_stack.top());
                    op_stack.pop();
                }
                op_stack.push(tok);
                break;
            }

            case TokKind::Eof:
                break;   // sentinel — nothing to do

            default:
                throw std::runtime_error("invalid token in WHERE clause: " + tok.raw);
        }
    }

    // Drain whatever remains on the operator stack.
    while (!op_stack.empty()) {
        if (op_stack.top().kind == TokKind::LParen)
            throw std::runtime_error("unmatched opening parenthesis");
        out_queue.push_back(op_stack.top());
        op_stack.pop();
    }

    return out_queue;
}

bool eval_rpn(const std::vector<Token>& rpn_expr,
              const Table& tbl_schema,
              const Record& rec) {
    std::stack<CellVal> val_stack;

    for (const Token& tok : rpn_expr) {
        // Non-operator → push its resolved value.
        if (tok.kind != TokKind::Op) {
            val_stack.push(resolve_token(tok, tbl_schema, rec));
            continue;
        }

        OpMeta meta = get_op_meta(tok.raw);

        // Unary NOT.
        if (meta.unary) {
            if (val_stack.empty())
                throw std::runtime_error("NOT operator has no operand");
            bool bv = to_bool(val_stack.top());
            val_stack.pop();
            val_stack.push(CellVal{static_cast<long long>(!bv)});
            continue;
        }

        // Binary operator — need at least two values on the stack.
        if (val_stack.size() < 2)
            throw std::runtime_error("binary operator missing operands: " + tok.raw);

        CellVal rhs = val_stack.top(); val_stack.pop();
        CellVal lhs = val_stack.top(); val_stack.pop();

        // Short-circuit-style logical operators.
        if (tok.raw == "AND") {
            val_stack.push(CellVal{static_cast<long long>(
                to_bool(lhs) && to_bool(rhs))});
            continue;
        }
        if (tok.raw == "OR") {
            val_stack.push(CellVal{static_cast<long long>(
                to_bool(lhs) || to_bool(rhs))});
            continue;
        }

        // Comparison operators.
        bool type_ok = true;
        int  cmp     = cmp_values(lhs, rhs, type_ok);
        bool result  = false;

        if (type_ok) {
            if      (tok.raw == "=")  result = (cmp == 0);
            else if (tok.raw == "!=") result = (cmp != 0);
            else if (tok.raw == "<")  result = (cmp <  0);
            else if (tok.raw == "<=") result = (cmp <= 0);
            else if (tok.raw == ">")  result = (cmp >  0);
            else if (tok.raw == ">=") result = (cmp >= 0);
        }

        val_stack.push(CellVal{static_cast<long long>(result)});
    }

    // An empty predicate is treated as universally true.
    if (val_stack.empty()) return true;
    return to_bool(val_stack.top());
}

std::string rpn_to_string(const std::vector<Token>& rpn_expr) {
    std::string out;
    for (const Token& tok : rpn_expr) {
        if (!out.empty()) out += ' ';
        // Re-quote string literals for readability.
        out += (tok.kind == TokKind::StrLit) ? ("'" + tok.raw + "'") : tok.raw;
    }
    return out;
}

// ---------------------------------------------------------------------------
// SELECT parser
// ---------------------------------------------------------------------------

QueryPlan parse_select(const std::string& sql) {
    std::vector<Token> tok_list = tokenize(sql);
    QueryPlan plan;
    std::size_t cur = 0;  // current position in tok_list

    // Helper: assert the current token has the expected kind.
    auto require = [&](TokKind expected, const char* label) {
        if (tok_list[cur].kind != expected)
            throw std::runtime_error(std::string("expected ") + label);
    };

    // --- SELECT ---
    require(TokKind::KwSelect, "SELECT");
    ++cur;

    // Projection list: either '*' or a comma-separated column list.
    if (tok_list[cur].kind == TokKind::Star) {
        ++cur;   // SELECT * → keep plan.select_cols empty
    } else {
        while (true) {
            require(TokKind::Ident, "column name");
            plan.select_cols.push_back(tok_list[cur].raw);
            ++cur;
            if (tok_list[cur].kind == TokKind::Comma) { ++cur; continue; }
            break;
        }
    }

    // --- FROM table ---
    require(TokKind::KwFrom, "FROM");  ++cur;
    require(TokKind::Ident,  "table name");
    plan.src_table = tok_list[cur].raw;
    ++cur;

    // --- Optional WHERE clause ---
    // Collect infix tokens until we hit ORDER, LIMIT, or end-of-input.
    if (tok_list[cur].kind == TokKind::KwWhere) {
        ++cur;
        std::vector<Token> where_infix;
        while (tok_list[cur].kind != TokKind::KwOrder  &&
               tok_list[cur].kind != TokKind::KwLimit  &&
               tok_list[cur].kind != TokKind::Eof)
            where_infix.push_back(tok_list[cur++]);
        plan.where_rpn = shunting_yard(where_infix);
    }

    // --- Optional ORDER BY col [ASC|DESC] ---
    if (tok_list[cur].kind == TokKind::KwOrder) {
        ++cur;
        require(TokKind::KwBy, "BY");
        ++cur;
        require(TokKind::Ident, "column name after ORDER BY");
        plan.sort_col = tok_list[cur].raw;
        ++cur;
        if      (tok_list[cur].kind == TokKind::KwAsc)  ++cur;
        else if (tok_list[cur].kind == TokKind::KwDesc) { plan.sort_desc = true; ++cur; }
    }

    // --- Optional LIMIT n ---
    if (tok_list[cur].kind == TokKind::KwLimit) {
        ++cur;
        require(TokKind::IntLit, "integer after LIMIT");
        plan.row_cap = tok_list[cur].num;
        ++cur;
    }

    require(TokKind::Eof, "end of query");
    return plan;
}

// ---------------------------------------------------------------------------
// Executor: filter → project → sort → cap
// ---------------------------------------------------------------------------

Table execute(const QueryPlan& plan, const Table& source) {
    if (plan.src_table != source.name)
        throw std::runtime_error("table not found: " + plan.src_table);

    // Determine output columns and their source positions.
    std::vector<std::string> out_header =
        plan.select_cols.empty() ? source.header : plan.select_cols;

    std::vector<int> field_map;
    field_map.reserve(out_header.size());
    for (const std::string& col : out_header) {
        int p = source.col_pos(col);
        if (p < 0)
            throw std::runtime_error("projected column not found: " + col);
        field_map.push_back(p);
    }

    Table result;
    result.name   = source.name;
    result.header = out_header;

    // Step 1 — Filter (WHERE) and project.
    for (const Record& rec : source.data) {
        if (!plan.where_rpn.empty() &&
            !eval_rpn(plan.where_rpn, source, rec))
            continue;

        Record projected;
        projected.fields.reserve(field_map.size());
        for (int idx : field_map)
            projected.fields.push_back(rec.fields[static_cast<std::size_t>(idx)]);
        result.data.push_back(std::move(projected));
    }

    // Step 2 — Sort (ORDER BY).
    if (!plan.sort_col.empty()) {
        int sort_pos = result.col_pos(plan.sort_col);
        if (sort_pos < 0)
            throw std::runtime_error("ORDER BY column absent from projection: " +
                                     plan.sort_col);

        std::stable_sort(result.data.begin(), result.data.end(),
            [&](const Record& a, const Record& b) {
                bool ok  = true;
                int  cmp = cmp_values(a.fields[static_cast<std::size_t>(sort_pos)],
                                      b.fields[static_cast<std::size_t>(sort_pos)],
                                      ok);
                return plan.sort_desc ? (cmp > 0) : (cmp < 0);
            });
    }

    // Step 3 — Cap (LIMIT).
    if (plan.row_cap >= 0 &&
        static_cast<long long>(result.data.size()) > plan.row_cap)
        result.data.resize(static_cast<std::size_t>(plan.row_cap));

    return result;
}

// ---------------------------------------------------------------------------
// Table display — fixed-width column grid
// ---------------------------------------------------------------------------

void print_table(const Table& tbl, std::ostream& os) {
    const std::size_t num_cols = tbl.header.size();

    // Compute per-column widths (start from header label width).
    std::vector<std::size_t> col_w(num_cols);
    for (std::size_t c = 0; c < num_cols; ++c)
        col_w[c] = tbl.header[c].size();

    // Helper: format a CellVal as a string.
    auto fmt_cell = [](const CellVal& v) -> std::string {
        if (std::holds_alternative<long long>(v))
            return std::to_string(std::get<long long>(v));
        return std::get<std::string>(v);
    };

    // Widen each column to accommodate its widest data cell.
    for (const Record& rec : tbl.data)
        for (std::size_t c = 0; c < num_cols; ++c)
            col_w[c] = std::max(col_w[c], fmt_cell(rec.fields[c]).size());

    // Print with trailing spaces so columns are padded to col_w[c].
    auto print_padded = [&](const std::string& s, std::size_t w) {
        os << s << std::string(w - s.size(), ' ');
    };

    // Header row.
    for (std::size_t c = 0; c < num_cols; ++c) {
        print_padded(tbl.header[c], col_w[c]);
        os << (c + 1 < num_cols ? " | " : "\n");
    }

    // Separator line.
    for (std::size_t c = 0; c < num_cols; ++c) {
        os << std::string(col_w[c], '-');
        os << (c + 1 < num_cols ? "-+-" : "\n");
    }

    // Data rows.
    for (const Record& rec : tbl.data) {
        for (std::size_t c = 0; c < num_cols; ++c) {
            print_padded(fmt_cell(rec.fields[c]), col_w[c]);
            os << (c + 1 < num_cols ? " | " : "\n");
        }
    }

    os << "(" << tbl.data.size()
       << " row" << (tbl.data.size() == 1 ? "" : "s") << ")\n";
}

}  // namespace queryengine
