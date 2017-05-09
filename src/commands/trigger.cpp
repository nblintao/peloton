#include "commands/trigger.h"
#include "parser/pg_trigger.h"

namespace peloton {
namespace commands {
Trigger::Trigger(const peloton::planner::CreatePlan& plan) {
  trigger_name = plan.GetTriggerName();
  trigger_funcname = plan.GetTriggerFuncName();
  trigger_args = plan.GetTriggerArgs();
  trigger_columns = plan.GetTriggerColumns();
  trigger_when = plan.GetTriggerWhen();
  trigger_type = plan.GetTriggerType();
}

void TriggerList::AddTrigger(Trigger trigger) {
  triggers.push_back(trigger);
  UpdateTypeSummary(trigger.GetTriggerType());
}

void TriggerList::UpdateTypeSummary(int16_t type) {
  types_summary[BEFORE_INSERT_ROW] |= TRIGGER_TYPE_MATCHES(
      type, TRIGGER_TYPE_ROW, TRIGGER_TYPE_BEFORE, TRIGGER_TYPE_INSERT);
  types_summary[BEFORE_INSERT_STATEMENT] |= TRIGGER_TYPE_MATCHES(
      type, TRIGGER_TYPE_STATEMENT, TRIGGER_TYPE_BEFORE, TRIGGER_TYPE_INSERT);
  types_summary[BEFORE_UPDATE_ROW] |= TRIGGER_TYPE_MATCHES(
      type, TRIGGER_TYPE_ROW, TRIGGER_TYPE_BEFORE, TRIGGER_TYPE_UPDATE);
  types_summary[BEFORE_UPDATE_STATEMENT] |= TRIGGER_TYPE_MATCHES(
      type, TRIGGER_TYPE_STATEMENT, TRIGGER_TYPE_BEFORE, TRIGGER_TYPE_UPDATE);
  types_summary[BEFORE_DELETE_ROW] |= TRIGGER_TYPE_MATCHES(
      type, TRIGGER_TYPE_ROW, TRIGGER_TYPE_BEFORE, TRIGGER_TYPE_DELETE);
  types_summary[BEFORE_DELETE_STATEMENT] |= TRIGGER_TYPE_MATCHES(
      type, TRIGGER_TYPE_STATEMENT, TRIGGER_TYPE_BEFORE, TRIGGER_TYPE_DELETE);
  types_summary[AFTER_INSERT_ROW] |= TRIGGER_TYPE_MATCHES(
      type, TRIGGER_TYPE_ROW, TRIGGER_TYPE_AFTER, TRIGGER_TYPE_INSERT);
  types_summary[AFTER_INSERT_STATEMENT] |= TRIGGER_TYPE_MATCHES(
      type, TRIGGER_TYPE_STATEMENT, TRIGGER_TYPE_AFTER, TRIGGER_TYPE_INSERT);
  types_summary[AFTER_UPDATE_ROW] |= TRIGGER_TYPE_MATCHES(
      type, TRIGGER_TYPE_ROW, TRIGGER_TYPE_AFTER, TRIGGER_TYPE_UPDATE);
  types_summary[AFTER_UPDATE_STATEMENT] |= TRIGGER_TYPE_MATCHES(
      type, TRIGGER_TYPE_STATEMENT, TRIGGER_TYPE_AFTER, TRIGGER_TYPE_UPDATE);
  types_summary[AFTER_DELETE_ROW] |= TRIGGER_TYPE_MATCHES(
      type, TRIGGER_TYPE_ROW, TRIGGER_TYPE_AFTER, TRIGGER_TYPE_DELETE);
  types_summary[AFTER_DELETE_STATEMENT] |= TRIGGER_TYPE_MATCHES(
      type, TRIGGER_TYPE_STATEMENT, TRIGGER_TYPE_AFTER, TRIGGER_TYPE_DELETE);
}

/**
 * execute trigger on each row before inserting tuple.
 */
storage::Tuple* TriggerList::ExecBRInsertTriggers(storage::Tuple *tuple, executor::ExecutorContext *executor_context_) {
  unsigned i;
  LOG_INFO("enter into ExecBRInsertTriggers");

  //check valid type
  if (!types_summary[EnumTriggerType::BEFORE_INSERT_ROW]) {
    return nullptr;
  }

  storage::Tuple* new_tuple = tuple;
  for (i = 0; i < triggers.size(); i++) {
    Trigger obj = triggers[i];

    //check valid type
    if (!TRIGGER_TYPE_MATCHES(obj.GetTriggerType(), TRIGGER_TYPE_ROW, TRIGGER_TYPE_BEFORE, TRIGGER_TYPE_INSERT)) {
      continue;
    }

    //TODO: check if trigger is enabled
    LOG_INFO("==================");
    expression::AbstractExpression* predicate_ = obj.GetTriggerWhen();
    if (predicate_ != nullptr) {
      if (executor_context_ != nullptr) {
        LOG_INFO("before evalulate");
        auto number = predicate_->GetChildrenSize();
        (void) number;
        auto eval = predicate_->Evaluate(tuple, nullptr, executor_context_);
        LOG_INFO("Evaluation result: %s", eval.GetInfo().c_str());
        if (eval.IsTrue()) {
          continue;
        }
      }
    }
    (void) executor_context_;
    LOG_INFO("==================");

    // apply all per-row-before-insert triggers on the tuple
    new_tuple = obj.ExecCallTriggerFunc(tuple);
  }
  LOG_INFO("out of ExecBRInsertTriggers");
  return new_tuple;
}

/**
 * Call a trigger function.
 */
storage::Tuple* Trigger::ExecCallTriggerFunc(storage::Tuple *tuple) {
  LOG_INFO("enter into ExecCallTriggerFunc");
  //TODO: call UDF function.
  return tuple;
}

}
}
