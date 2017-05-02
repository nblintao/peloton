//===----------------------------------------------------------------------===//
//
//                         Peloton
//
// query_metrics_catalog.cpp
//
// Identification: src/catalog/query_metrics_catalog.cpp
//
// Copyright (c) 2015-17, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include "catalog/catalog.h"
#include "catalog/trigger_catalog.h"
#include "common/macros.h"
#include "common/logger.h"

namespace peloton {
namespace catalog {

TriggerCatalog *TriggerCatalog::GetInstance(
    concurrency::Transaction *txn) {
  static std::unique_ptr<TriggerCatalog> trigger_catalog(
      new TriggerCatalog(txn));

  return trigger_catalog.get();
}

TriggerCatalog::TriggerCatalog(concurrency::Transaction *txn)
    : AbstractCatalog("CREATE TABLE " CATALOG_DATABASE_NAME
                      "." TRIGGER_CATALOG_NAME
                      " ("
                      "trigger_name          VARCHAR NOT NULL, "
                      "database_oid          INT NOT NULL, "
                      "table_oid             INT NOT NULL, "
                      "trigger_type          INT NOT NULL, "
                      "fire_condition        VARCHAR, "
                      "function_name         VARCHAR, "
                      "function_arguments    VARCHAR, "
                      "time_stamp            INT NOT NULL);",
                      txn) {
  // Add secondary index here if necessary
  Catalog::GetInstance()->CreateIndex(
      CATALOG_DATABASE_NAME, TRIGGER_CATALOG_NAME,
      {"database_oid", "table_oid", "trigger_type"}, TRIGGER_CATALOG_NAME "_skey0",
      false, IndexType::BWTREE, txn);
}

TriggerCatalog::~TriggerCatalog() {}

bool TriggerCatalog::InsertTrigger(
    std::string trigger_name, oid_t database_oid, oid_t table_oid,
    commands::EnumTriggerType trigger_type,
    std::string fire_condition, //TODO: this actually should be expression
    std::string function_name,
    std::string function_arguments,
    int64_t time_stamp, type::AbstractPool *pool,
    concurrency::Transaction *txn) {


  std::unique_ptr<storage::Tuple> tuple(
      new storage::Tuple(catalog_table_->GetSchema(), true));

  auto val0 = type::ValueFactory::GetVarcharValue(trigger_name, pool);
  auto val1 = type::ValueFactory::GetIntegerValue(database_oid);
  auto val2 = type::ValueFactory::GetIntegerValue(table_oid);
  auto val3 = type::ValueFactory::GetIntegerValue(trigger_type);
  auto val4 = type::ValueFactory::GetVarcharValue(fire_condition, pool);
  auto val5 = type::ValueFactory::GetVarcharValue(function_name, pool);
  auto val6 = type::ValueFactory::GetVarcharValue(function_arguments, pool);
  auto val7 = type::ValueFactory::GetIntegerValue(time_stamp);

  tuple->SetValue(ColumnId::TRIGGER_NAME, val0, pool);
  tuple->SetValue(ColumnId::DATABASE_OID, val1, pool);
  tuple->SetValue(ColumnId::TABLE_OID, val2, pool);
  tuple->SetValue(ColumnId::TRIGGER_TYPE, val3, pool);
  tuple->SetValue(ColumnId::FIRE_CONDITION, val4, pool);
  tuple->SetValue(ColumnId::FUNCTION_NAME, val5, pool);
  tuple->SetValue(ColumnId::FUNCTION_ARGS, val6, pool);
  tuple->SetValue(ColumnId::TIME_STAMP, val7, pool);

  // Insert the tuple
  return InsertTuple(std::move(tuple), txn);
}

commands::TriggerList* TriggerCatalog::GetTriggers(oid_t database_oid,
                                          oid_t table_oid,
                                          commands::EnumTriggerType trigger_type,
                                          concurrency::Transaction *txn) {
  // select trigger_name, fire condition, function_name, function_args
  std::vector<oid_t> column_ids({ColumnId::TRIGGER_NAME, ColumnId::FIRE_CONDITION, ColumnId::FUNCTION_NAME, ColumnId::FUNCTION_ARGS});
  oid_t index_offset = IndexId::SECONDARY_KEY_0;          // Secondary key index
  std::vector<type::Value> values;
  // where database_oid = args.database_oid and table_oid = args.table_oid and trigger_type = args.trigger_type
  values.push_back(type::ValueFactory::GetIntegerValue(database_oid).Copy());
  values.push_back(type::ValueFactory::GetIntegerValue(table_oid).Copy());
  values.push_back(type::ValueFactory::GetIntegerValue(trigger_type).Copy());

  // the result is a vector of executor::LogicalTile
  auto result_tiles =
      GetResultWithIndexScan(column_ids, index_offset, values, txn);
  LOG_INFO("size of the result tiles = %lu", result_tiles->size());

  // create the trigger list
  commands::TriggerList newTriggerList;
  for (unsigned int i = 0; i < result_tiles->size(); i++) {
    size_t tuple_count = (*result_tiles)[i]->GetTupleCount();
    for (size_t j = 0; j < tuple_count; j++) {
      LOG_INFO("trigger_name is %s", (*result_tiles)[i]->GetValue(j, 0).ToString().c_str());
      LOG_INFO("FIRE_CONDITION is %s", (*result_tiles)[i]->GetValue(j, 1).ToString().c_str());
      LOG_INFO("FUNCTION_NAME is %s", (*result_tiles)[i]->GetValue(j, 2).ToString().c_str());
      LOG_INFO("FUNCTION_ARGS is %s", (*result_tiles)[i]->GetValue(j, 3).ToString().c_str());

      // create a new trigger instance

    }
  }

  // int64_t num_params = 0;
  // PL_ASSERT(result_tiles->size() <= 1);  // unique
  // if (result_tiles->size() != 0) {
  //   PL_ASSERT((*result_tiles)[0]->GetTupleCount() <= 1);
  //   if ((*result_tiles)[0]->GetTupleCount() != 0) {
  //     num_params = (*result_tiles)[0]
  //                      ->GetValue(0, 0)
  //                      .GetAs<int>();  // After projection left 1 column
  //   }
  // }

  return nullptr;
}

// stats::QueryMetric::QueryParamBuf QueryMetricsCatalog::GetParamTypes(
//     const std::string &name, oid_t database_oid,
//     concurrency::Transaction *txn) {
//   std::vector<oid_t> column_ids({ColumnId::PARAM_TYPES});  // param_types
//   oid_t index_offset = IndexId::SECONDARY_KEY_0;  // Secondary key index
//   std::vector<type::Value> values;
//   values.push_back(type::ValueFactory::GetVarcharValue(name, nullptr).Copy());
//   values.push_back(type::ValueFactory::GetIntegerValue(database_oid).Copy());

//   auto result_tiles =
//       GetResultWithIndexScan(column_ids, index_offset, values, txn);

//   stats::QueryMetric::QueryParamBuf param_types;
//   PL_ASSERT(result_tiles->size() <= 1);  // unique
//   if (result_tiles->size() != 0) {
//     PL_ASSERT((*result_tiles)[0]->GetTupleCount() <= 1);
//     if ((*result_tiles)[0]->GetTupleCount() != 0) {
//       auto param_types_value = (*result_tiles)[0]->GetValue(0, 0);
//       param_types.buf = const_cast<uchar *>(
//           reinterpret_cast<const uchar *>(param_types_value.GetData()));
//       param_types.len = param_types_value.GetLength();
//     }
//   }

//   return param_types;
// }

// int64_t QueryMetricsCatalog::GetNumParams(const std::string &name,
//                                           oid_t database_oid,
//                                           concurrency::Transaction *txn) {
//   std::vector<oid_t> column_ids({ColumnId::NUM_PARAMS});  // num_params
//   oid_t index_offset = IndexId::SECONDARY_KEY_0;          // Secondary key index
//   std::vector<type::Value> values;
//   values.push_back(type::ValueFactory::GetVarcharValue(name, nullptr).Copy());
//   values.push_back(type::ValueFactory::GetIntegerValue(database_oid).Copy());

//   auto result_tiles =
//       GetResultWithIndexScan(column_ids, index_offset, values, txn);

//   int64_t num_params = 0;
//   PL_ASSERT(result_tiles->size() <= 1);  // unique
//   if (result_tiles->size() != 0) {
//     PL_ASSERT((*result_tiles)[0]->GetTupleCount() <= 1);
//     if ((*result_tiles)[0]->GetTupleCount() != 0) {
//       num_params = (*result_tiles)[0]
//                        ->GetValue(0, 0)
//                        .GetAs<int>();  // After projection left 1 column
//     }
//   }

//   return num_params;
// }

}  // End catalog namespace
}  // End peloton namespace
