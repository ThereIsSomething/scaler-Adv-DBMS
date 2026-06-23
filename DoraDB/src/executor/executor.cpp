#include "executor/executor.h"
#include <cstring>
#include <stdexcept>

// ============================================================
// SeqScanNode
// ============================================================

SeqScanNode::SeqScanNode(HeapFile *heap, const Schema &schema)
    : heap_(heap), schema_(schema) {}

void SeqScanNode::Open() {
  rows_.clear();
  cursor_ = 0;
  heap_->Scan([&](const RID &rid, const char *data, int size) {
    OutputRow out;
    out.values = DeserializeRow(data, size, schema_);
    out.rid = rid;
    rows_.push_back(out);
  });
}

bool SeqScanNode::Next(OutputRow &out) {
  if (cursor_ >= (int)rows_.size())
    return false;
  out = rows_[cursor_++];
  return true;
}

void SeqScanNode::Close() {
  rows_.clear();
  cursor_ = 0;
}

// ============================================================
// IndexScanNode
// ============================================================

IndexScanNode::IndexScanNode(BPlusTree *index, HeapFile *heap,
                             const Schema &schema, int low_key, int high_key,
                             bool exact)
    : index_(index), heap_(heap), schema_(schema), low_key_(low_key),
      high_key_(high_key), exact_(exact) {}

void IndexScanNode::Open() {
  rows_.clear();
  cursor_ = 0;
  std::vector<RID> rids;
  if (exact_) {
    auto r = index_->Search(low_key_);
    if (r)
      rids.push_back(*r);
  } else {
    rids = index_->RangeScan(low_key_, high_key_);
  }
  char buf[PAGE_SIZE];
  int size;
  for (auto &rid : rids) {
    if (heap_->GetRow(rid, buf, &size)) {
      OutputRow out;
      out.values = DeserializeRow(buf, size, schema_);
      out.rid = rid;
      rows_.push_back(out);
    }
  }
}

bool IndexScanNode::Next(OutputRow &out) {
  if (cursor_ >= (int)rows_.size())
    return false;
  out = rows_[cursor_++];
  return true;
}

void IndexScanNode::Close() {
  rows_.clear();
  cursor_ = 0;
}

// ============================================================
// FilterNode
// ============================================================

FilterNode::FilterNode(std::unique_ptr<PlanNode> child, ExprPtr predicate,
                       const Schema &schema)
    : child_(std::move(child)), pred_(predicate), schema_(schema) {}

void FilterNode::Open() { child_->Open(); }
void FilterNode::Close() { child_->Close(); }

bool FilterNode::Next(OutputRow &out) {
  while (child_->Next(out)) {
    if (EvaluateExpr(pred_, out.values, schema_))
      return true;
  }
  return false;
}

// ============================================================
// ProjectionNode
// ============================================================

ProjectionNode::ProjectionNode(std::unique_ptr<PlanNode> child,
                               std::vector<int> col_indices)
    : child_(std::move(child)), col_indices_(std::move(col_indices)) {}

void ProjectionNode::Open() { child_->Open(); }
void ProjectionNode::Close() { child_->Close(); }

bool ProjectionNode::Next(OutputRow &out) {
  if (!child_->Next(out))
    return false;
  Row projected;
  for (int i : col_indices_) {
    if (i < (int)out.values.size())
      projected.push_back(out.values[i]);
  }
  out.values = projected;
  return true;
}

// ============================================================
// NestedLoopJoinNode
// ============================================================

NestedLoopJoinNode::NestedLoopJoinNode(std::unique_ptr<PlanNode> outer,
                                       std::unique_ptr<PlanNode> inner,
                                       int outer_col, int inner_col)
    : outer_(std::move(outer)), inner_(std::move(inner)), outer_col_(outer_col),
      inner_col_(inner_col) {}

void NestedLoopJoinNode::Open() {
  outer_->Open();
  has_outer_ = false;
}

void NestedLoopJoinNode::Close() {
  outer_->Close();
  inner_->Close();
}

bool NestedLoopJoinNode::Next(OutputRow &out) {
  while (true) {
    if (!has_outer_) {
      if (!outer_->Next(current_outer_))
        return false;
      has_outer_ = true;
      inner_->Close();
      inner_->Open();
    }
    OutputRow inner_row;
    if (inner_->Next(inner_row)) {
      // Check equi-join condition
      if (outer_col_ < (int)current_outer_.values.size() &&
          inner_col_ < (int)inner_row.values.size() &&
          current_outer_.values[outer_col_] == inner_row.values[inner_col_]) {
        // Concatenate: outer columns + inner columns
        out.values = current_outer_.values;
        out.values.insert(out.values.end(), inner_row.values.begin(),
                          inner_row.values.end());
        out.rid = current_outer_.rid;
        return true;
      }
    } else {
      has_outer_ = false;
    }
  }
}

// ============================================================
// Expression evaluation
// ============================================================

Value EvalValue(const ExprPtr &expr, const Row &row, const Schema &schema) {
  if (expr->type == Expr::LITERAL)
    return expr->value;
  if (expr->type == Expr::COLUMN_REF) {
    int idx = schema.FindColumn(expr->column_name);
    if (idx < 0) {
      throw std::runtime_error("Unknown column: " + expr->column_name);
    }
    return row[idx];
  }
  throw std::runtime_error("Cannot evaluate expression as value");
}

bool EvaluateExpr(const ExprPtr &expr, const Row &row, const Schema &schema) {
  if (!expr)
    return true;
  switch (expr->type) {
  case Expr::AND_EXPR:
    return EvaluateExpr(expr->left, row, schema) &&
           EvaluateExpr(expr->right, row, schema);
  case Expr::OR_EXPR:
    return EvaluateExpr(expr->left, row, schema) ||
           EvaluateExpr(expr->right, row, schema);
  case Expr::COMPARE: {
    Value l = EvalValue(expr->left, row, schema);
    Value r = EvalValue(expr->right, row, schema);
    if (expr->op == "=")
      return l == r;
    if (expr->op == "!=")
      return l != r;
    if (expr->op == "<")
      return l < r;
    if (expr->op == ">")
      return l > r;
    if (expr->op == "<=")
      return l <= r;
    if (expr->op == ">=")
      return l >= r;
    return false;
  }
  default:
    return true;
  }
}

// JOIN-aware evaluation: resolves table.col references across two schemas
Value EvalValueJoin(const ExprPtr &expr, const Row &row, const Schema &ls,
                    const std::string &lt, const Schema &rs,
                    const std::string &rt) {
  if (expr->type == Expr::LITERAL)
    return expr->value;
  if (expr->type == Expr::COLUMN_REF) {
    // If table prefix matches, search in that schema
    if (expr->table_name == lt || expr->table_name.empty()) {
      int idx = ls.FindColumn(expr->column_name);
      if (idx >= 0)
        return row[idx];
    }
    if (expr->table_name == rt || expr->table_name.empty()) {
      int idx = rs.FindColumn(expr->column_name);
      if (idx >= 0)
        return row[ls.columns.size() + idx];
    }
    throw std::runtime_error(
        "Unknown column: " +
        (expr->table_name.empty() ? "" : expr->table_name + ".") +
        expr->column_name);
  }
  throw std::runtime_error("Cannot evaluate expression as value");
}

bool EvaluateExprJoin(const ExprPtr &expr, const Row &row, const Schema &ls,
                      const std::string &lt, const Schema &rs,
                      const std::string &rt) {
  if (!expr)
    return true;
  switch (expr->type) {
  case Expr::AND_EXPR:
    return EvaluateExprJoin(expr->left, row, ls, lt, rs, rt) &&
           EvaluateExprJoin(expr->right, row, ls, lt, rs, rt);
  case Expr::OR_EXPR:
    return EvaluateExprJoin(expr->left, row, ls, lt, rs, rt) ||
           EvaluateExprJoin(expr->right, row, ls, lt, rs, rt);
  case Expr::COMPARE: {
    Value l = EvalValueJoin(expr->left, row, ls, lt, rs, rt);
    Value r = EvalValueJoin(expr->right, row, ls, lt, rs, rt);
    if (expr->op == "=")
      return l == r;
    if (expr->op == "!=")
      return l != r;
    if (expr->op == "<")
      return l < r;
    if (expr->op == ">")
      return l > r;
    if (expr->op == "<=")
      return l <= r;
    if (expr->op == ">=")
      return l >= r;
    return false;
  }
  default:
    return true;
  }
}
