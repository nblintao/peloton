/*-------------------------------------------------------------------------
 *
 * tile_group.cpp
 * file description
 *
 * Copyright(c) 2015, CMU
 *
 * /n-store/src/storage/tile_group.cpp
 *
 *-------------------------------------------------------------------------
 */

#include "backend/storage/tile_group.h"

#include <numeric>

#include "backend/common/logger.h"
#include "backend/common/synch.h"
#include "backend/common/types.h"
#include "backend/storage/abstract_table.h"

namespace peloton {
namespace storage {

TileGroup::TileGroup(TileGroupHeader* tile_group_header,
                     AbstractTable *table,
                     AbstractBackend* backend,
                     const std::vector<catalog::Schema>& schemas,
                     int tuple_count)
: database_id(INVALID_OID),
  table_id(INVALID_OID),
  tile_group_id(INVALID_OID),
  backend(backend),
  tile_schemas(schemas),
  tile_group_header(tile_group_header),
  table(table),
  num_tuple_slots(tuple_count) {

  tile_count = tile_schemas.size();

  for(oid_t tile_itr = 0 ; tile_itr < tile_count ; tile_itr++){

    auto& manager = catalog::Manager::GetInstance();
    oid_t tile_id = manager.GetNextOid();

    Tile *tile = storage::TileFactory::GetTile(
        database_id, table_id, tile_group_id, tile_id,
        tile_group_header,
        backend,
        tile_schemas[tile_itr],
        this,
        tuple_count);

    tiles.push_back(tile);
  }

}

//===--------------------------------------------------------------------===//
// Operations
//===--------------------------------------------------------------------===//

/**
 * Grab next slot (thread-safe) and fill in the tuple
 *
 * Returns slot where inserted (INVALID_ID if not inserted)
 */
oid_t TileGroup::InsertTuple(txn_id_t transaction_id, const Tuple *tuple) {

  oid_t tuple_slot_id = tile_group_header->GetNextEmptyTupleSlot();

  LOG_TRACE("Tile Group Id :: %lu status :: %lu out of %lu slots \n", tile_group_id, tuple_slot_id, num_tuple_slots);

  // No more slots
  if(tuple_slot_id == INVALID_OID)
    return INVALID_OID;

  oid_t tile_column_count;
  oid_t column_itr = 0;

  for(oid_t tile_itr = 0 ; tile_itr < tile_count ; tile_itr++){
    const catalog::Schema& schema = tile_schemas[tile_itr];
    tile_column_count = schema.GetColumnCount();

    storage::Tile *tile = GetTile(tile_itr);
    assert(tile);
    char* tile_tuple_location = tile->GetTupleLocation(tuple_slot_id);
    assert(tile_tuple_location);

    // NOTE:: Only a tuple wrapper
    storage::Tuple tile_tuple(&schema, tile_tuple_location);

    for(oid_t tile_column_itr = 0 ; tile_column_itr < tile_column_count ; tile_column_itr++){
      tile_tuple.SetValueAllocate(tile_column_itr, tuple->GetValue(column_itr), tile->GetPool());
      column_itr++;
    }
  }

  // Set MVCC info
  tile_group_header->SetTransactionId(tuple_slot_id, transaction_id);
  tile_group_header->SetBeginCommitId(tuple_slot_id, MAX_CID);
  tile_group_header->SetEndCommitId(tuple_slot_id, MAX_CID);
  tile_group_header->SetPrevItemPointer(tuple_slot_id, INVALID_ITEMPOINTER);

  return tuple_slot_id;
}

void TileGroup::ReclaimTuple(oid_t tuple_slot_id) {

  // add it to free slots
  tile_group_header->ReclaimTupleSlot(tuple_slot_id);

}

Tuple *TileGroup::SelectTuple(oid_t tile_offset, oid_t tuple_slot_id) {
  assert(tile_offset < tile_count);
  assert(tuple_slot_id < num_tuple_slots);

  // is it within bounds ?
  if(tuple_slot_id >= GetNextTupleSlot())
    return nullptr;

  Tile *tile = GetTile(tile_offset);
  assert(tile);
  Tuple *tuple = tile->GetTuple(tuple_slot_id);
  return tuple;
}

Tuple *TileGroup::SelectTuple(oid_t tuple_slot_id){

  // is it within bounds ?
  if(tuple_slot_id >= GetNextTupleSlot())
    return nullptr;

  // allocate a new copy of the original tuple
  Tuple *tuple = new Tuple(table->GetSchema(), true);
  oid_t tuple_attr_itr = 0;

  for(oid_t tile_itr = 0 ; tile_itr < tile_count ; tile_itr++){
    Tile *tile = GetTile(tile_itr);
    assert(tile);

    // tile tuple wrapper
    Tuple tile_tuple(tile->GetSchema(), tile->GetTupleLocation(tuple_slot_id));
    oid_t tile_tuple_count = tile->GetColumnCount();

    for(oid_t tile_tuple_attr_itr = 0 ; tile_tuple_attr_itr < tile_tuple_count ; tile_tuple_attr_itr++) {
      Value val = tile_tuple.GetValue(tile_tuple_attr_itr);
      tuple->SetValueAllocate(tuple_attr_itr++, val, nullptr);
    }
  }

  return tuple;
}

// delete tuple at given slot if it is not already locked
bool TileGroup::DeleteTuple(txn_id_t transaction_id, oid_t tuple_slot_id) {

  // compare and exchange the end commit id to start delete
  if (atomic_cas<txn_id_t>(tile_group_header->GetEndCommitIdLocation(tuple_slot_id),
                           MAX_CID, transaction_id)) {
    return true;
  }

  return false;
}

void TileGroup::CommitInsertedTuple(oid_t tuple_slot_id, cid_t commit_id){

  // set the begin commit id to persist insert
  tile_group_header->SetBeginCommitId(tuple_slot_id, commit_id);
  tile_group_header->IncrementActiveTupleCount();
}

void TileGroup::CommitDeletedTuple(oid_t tuple_slot_id, txn_id_t transaction_id __attribute__((unused)), cid_t commit_id){

  // set the end commit id to persist delete
  tile_group_header->SetEndCommitId(tuple_slot_id, commit_id);
  tile_group_header->DecrementActiveTupleCount();

}

void TileGroup::AbortInsertedTuple(oid_t tuple_slot_id){

  // undo insert (we don't reset MVCC info currently)
  ReclaimTuple(tuple_slot_id);

}

void TileGroup::AbortDeletedTuple(oid_t tuple_slot_id){

  // undo deletion
  tile_group_header->SetEndCommitId(tuple_slot_id, MAX_CID);

}

// Sets the tile id and column id w.r.t that tile corresponding to
// the specified tile group column id.
void TileGroup::LocateTileAndColumn(oid_t column_id, oid_t &tile_offset, oid_t &tile_column_id) {
  tile_column_id = column_id;
  tile_offset = 0;

  assert(tile_schemas.size() > 0);
  while (tile_column_id >= tile_schemas[tile_offset].GetColumnCount()) {
    tile_column_id -= tile_schemas[tile_offset].GetColumnCount();
    tile_offset++;
  }

  assert(tile_offset < tiles.size());
}

oid_t TileGroup::GetTileIdFromColumnId(oid_t column_id) {
  oid_t tile_column_id, tile_offset;
  LocateTileAndColumn(column_id, tile_offset, tile_column_id);
  return tile_offset;
}

oid_t TileGroup::GetTileColumnId(oid_t column_id) {
  oid_t tile_column_id, tile_offset;
  LocateTileAndColumn(column_id, tile_offset, tile_column_id);
  return tile_column_id;
}

Value TileGroup::GetValue(oid_t tuple_id, oid_t column_id) {
  assert(tuple_id < GetNextTupleSlot());
  oid_t tile_column_id, tile_offset;
  LocateTileAndColumn(column_id, tile_offset, tile_column_id);
  return GetTile(tile_offset)->GetValue(tuple_id, tile_column_id);
}

Tile *TileGroup::GetTile(const oid_t tile_offset) const {
  assert(tile_offset < tile_count);
  Tile *tile = tiles[tile_offset];
  return tile;
}


//===--------------------------------------------------------------------===//
// Utilities
//===--------------------------------------------------------------------===//

// Get a string representation of this tile group
std::ostream& operator<<(std::ostream& os, const TileGroup& tile_group) {

  os << "=============================================================\n";

  os << "TILE GROUP :\n";
  os << "\tCatalog ::"
      << " DB: "<< tile_group.database_id << " Table: " << tile_group.table_id
      << " Tile Group:  " << tile_group.tile_group_id
      << "\n";

  os << "\tActive Tuples:  " << tile_group.tile_group_header->GetActiveTupleCount()
      << " out of " << tile_group.num_tuple_slots  <<" slots\n";

  for(oid_t tile_itr = 0 ; tile_itr < tile_group.tile_count ; tile_itr++){
    Tile *tile = tile_group.GetTile(tile_itr);
    if(tile != nullptr)
      os << (*tile);
  }

  auto header = tile_group.GetHeader();
  if(header != nullptr)
    os << (*header);

  os << "=============================================================\n";

  return os;
}

} // End storage namespace
} // End peloton namespace