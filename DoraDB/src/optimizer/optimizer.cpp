#include "optimizer/optimizer.h"
#include "storage/heap_engine.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <iostream>

// ============================================================
// AnalyzeWhere — extract index-usable conditions from WHERE
//
// Looks for patterns like: pk_col = constant, pk_col > constant, etc.
// Everything else goes into remaining_filter.
// ============================================================

static void CollectConditions(const ExprPtr &expr,
                              std::vector<ExprPtr> &conditions) {
  if (!expr)
    return;
  if (expr->type == Expr::AND_EXPR) {
    CollectConditions(expr->left, conditions);
    CollectConditions(expr->right, conditions);
  } else {
    conditions.push_back(expr);
  }
}

static ExprPtr CombineWithAnd(const std::vector<ExprPtr> &exprs) {
  if (exprs.empty())
    return nullptr;
  ExprPtr result = exprs[0];
  for (size_t i = 1; i < exprs.size(); i++) {
    result = Expr::MakeAnd(result, exprs[i]);
  }
  return result;
}

IndexCondition AnalyzeWhere(const ExprPtr &where, const Schema &schema) {
  IndexCondition cond;
  if (!where || schema.pk_index < 0)
    return cond;

  std::string pk_name = schema.columns[schema.pk_index].name;
  std::vector<ExprPtr> all_conditions;
  CollectConditions(where, all_conditions);

  std::vector<ExprPtr> remaining;
  int low = INT_MIN, high = INT_MAX;
  bool has_eq = false;
  int eq_val = 0;

  for (auto &c : all_conditions) {
    if (c->type != Expr::COMPARE || !c->left || !c->right) {
      remaining.push_back(c);
      continue;
    }
    // Check if this is: pk_col OP literal
    bool is_pk_left =
        (c->left->type == Expr::COLUMN_REF && c->left->column_name == pk_name);
    bool is_lit_right = (c->right->type == Expr::LITERAL &&
                         c->right->value.type == DataType::INT);

    if (is_pk_left && is_lit_right) {
      int val = c->right->value.int_val;
      if (c->op == "=") {
        has_eq = true;
        eq_val = val;
      } else if (c->op == ">") {
        low = std::max(low, val + 1);
      } else if (c->op == ">=") {
        low = std::max(low, val);
      } else if (c->op == "<") {
        high = std::min(high, val - 1);
      } else if (c->op == "<=") {
        high = std::min(high, val);
      } else {
        remaining.push_back(c);
        continue;
      }
      cond.can_use_index = true;
    } else {
      remaining.push_back(c);
    }
  }

  if (has_eq) {
    cond.is_exact = true;
    cond.exact_key = eq_val;
  } else if (cond.can_use_index) {
    cond.range_low = low;
    cond.range_high = high;
  }

  cond.remaining_filter = CombineWithAnd(remaining);
  return cond;
}

// ============================================================
// Selectivity estimation
// ============================================================

double EstimateSelectivity(const IndexCondition &cond,
                           const TableStats &stats) {
  if (stats.row_count == 0)
    return 0.0;
  if (cond.is_exact) {
    return 1.0 / stats.row_count; // expect 1 row
  }
  // Range: fraction of the key domain
  double domain = stats.pk_max - stats.pk_min + 1;
  if (domain <= 0)
    return 1.0;
  double range = cond.range_high - cond.range_low + 1;
  return std::clamp(range / domain, 0.0, 1.0);
}

// ============================================================
// CreateSelectPlan — build optimal plan for a SELECT
//
// Decision process:
//   1. If WHERE references PK and index exists → IndexScan
//      Cost_index = tree_height + selectivity * num_pages
//   2. Otherwise → SeqScan
//      Cost_seq = num_pages
//   3. Wrap with FilterNode if remaining predicates
//   4. Handle JOIN (NLJ with smaller table as outer)
//   5. Wrap with ProjectionNode if specific columns
// ============================================================

std::unique_ptr<PlanNode> CreateSelectPlan(const SelectStmt &stmt,
                                           HeapEngine *engine) {
  const Schema &schema = engine->GetSchema(stmt.table_name);
  HeapFile *heap = engine->GetHeapFile(stmt.table_name);
  BPlusTree *index = engine->GetIndex(stmt.table_name);
  const TableStats &stats = engine->GetStats(stmt.table_name);

  // --- Build base scan node ---
  std::unique_ptr<PlanNode> plan;
  ExprPtr filter_expr = stmt.where_clause;

  if (!stmt.join.has_value()) {
    // Single-table query: decide SeqScan vs IndexScan
    IndexCondition idx_cond = AnalyzeWhere(stmt.where_clause, schema);

    if (idx_cond.can_use_index && index != nullptr) {
      double sel = EstimateSelectivity(idx_cond, stats);
      double index_cost =
          3 + sel * stats.num_pages; // tree height ~3 + fraction of pages
      double seq_cost = stats.num_pages;

      std::cout << "[Optimizer] SeqScan cost=" << seq_cost
                << ", IndexScan cost=" << index_cost << " → choosing "
                << (index_cost < seq_cost ? "IndexScan" : "SeqScan") << "\n";

      if (index_cost < seq_cost) {
        if (idx_cond.is_exact) {
          plan = std::make_unique<IndexScanNode>(index, heap, schema,
                                                 idx_cond.exact_key,
                                                 idx_cond.exact_key, true);
        } else {
          plan = std::make_unique<IndexScanNode>(index, heap, schema,
                                                 idx_cond.range_low,
                                                 idx_cond.range_high, false);
        }
        filter_expr = idx_cond.remaining_filter;
      }
    }

    if (!plan) {
      std::cout << "[Optimizer] Using SeqScan\n";
      plan = std::make_unique<SeqScanNode>(heap, schema);
    }

    // Apply remaining filter
    if (filter_expr) {
      plan = std::make_unique<FilterNode>(std::move(plan), filter_expr, schema);
    }
  } else {
    // --- JOIN query ---
    const auto &join = *stmt.join;
    const Schema &right_schema = engine->GetSchema(join.table_name);
    HeapFile *right_heap = engine->GetHeapFile(join.table_name);
    const TableStats &right_stats = engine->GetStats(join.table_name);

    auto left_scan = std::make_unique<SeqScanNode>(heap, schema);
    auto right_scan = std::make_unique<SeqScanNode>(right_heap, right_schema);

    // Find join column indices
    int left_col = -1, right_col = -1;
    std::string left_tbl =
        join.left_col.table.empty() ? stmt.table_name : join.left_col.table;
    std::string right_tbl =
        join.right_col.table.empty() ? join.table_name : join.right_col.table;

    if (left_tbl == stmt.table_name) {
      left_col = schema.FindColumn(join.left_col.column);
      right_col = right_schema.FindColumn(join.right_col.column);
    } else {
      left_col = schema.FindColumn(join.right_col.column);
      right_col = right_schema.FindColumn(join.left_col.column);
    }

    // NLJ outer pick: smaller table goes outer
    std::cout << "[Optimizer] JOIN: " << stmt.table_name << "("
              << stats.row_count << " rows)"
              << " ⋈ " << join.table_name << "(" << right_stats.row_count
              << " rows)"
              << " → outer="
              << (stats.row_count <= right_stats.row_count ? stmt.table_name
                                                           : join.table_name)
              << "\n";

    std::unique_ptr<PlanNode> outer, inner;
    int o_col, i_col;
    if (stats.row_count <= right_stats.row_count) {
      outer = std::move(left_scan);
      inner = std::move(right_scan);
      o_col = left_col;
      i_col = right_col;
    } else {
      outer = std::move(right_scan);
      inner = std::move(left_scan);
      o_col = right_col;
      i_col = left_col;
    }

    plan = std::make_unique<NestedLoopJoinNode>(std::move(outer),
                                                std::move(inner), o_col, i_col);

    // Apply WHERE filter on joined rows
    if (stmt.where_clause) {
      // Build combined schema for the join output
      Schema combined;
      combined.columns = schema.columns;
      combined.columns.insert(combined.columns.end(),
                              right_schema.columns.begin(),
                              right_schema.columns.end());
      plan = std::make_unique<FilterNode>(std::move(plan), stmt.where_clause,
                                          combined);
    }
  }

  // --- Projection (if not SELECT *) ---
  if (!stmt.select_all && !stmt.columns.empty()) {
    Schema combined_schema;
    combined_schema.columns = schema.columns;
    if (stmt.join.has_value()) {
      const Schema &rs = engine->GetSchema(stmt.join->table_name);
      combined_schema.columns.insert(combined_schema.columns.end(),
                                     rs.columns.begin(), rs.columns.end());
    }

    std::vector<int> indices;
    for (auto &ref : stmt.columns) {
      int idx = combined_schema.FindColumn(ref.column);
      if (idx >= 0)
        indices.push_back(idx);
    }
    if (!indices.empty()) {
      plan = std::make_unique<ProjectionNode>(std::move(plan), indices);
    }
  }

  return plan;
}
