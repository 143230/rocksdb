//  Copyright (c) 2013, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
#include "rocksdb/memtablerep.h"
#include "db/memtable.h"
#include "util/lockfreeSkipList.h"
#include "util/arena.h"
#include "util/coding.h"
#include "util/concurrent_arena.h"

namespace rocksdb {
namespace {
class LockFreeSkipListRep : public MemTableRep {
 private:
  SimpleConcurrentArena my_arena_;
  LockFreeSkipList<char*, const MemTableRep::KeyComparator&> skip_list_;

 public:
  explicit LockFreeSkipListRep(const MemTableRep::KeyComparator& compare,
                               MemTableAllocator* allocator)
      : MemTableRep(allocator),
        my_arena_(false),
        skip_list_(compare, &my_arena_) {}

  KeyHandle Allocate(const size_t len, char** buf) override {
    *buf = my_arena_.Allocate(len);
    return static_cast<KeyHandle>(*buf);
  }
  // Insert key into the list.
  // REQUIRES: nothing that compares equal to key is currently in the list.
  virtual void Insert(KeyHandle handle) override {
    skip_list_.insert(static_cast<char*>(handle));
  }

  // Returns true iff an entry that compares equal to key is in the list.
  virtual bool Contains(const char* key) const override {
    return skip_list_.contains((char* const)key);
  }

  virtual size_t ApproximateMemoryUsage() override {
    return my_arena_.ApproximateMemoryUsage();
  }

  virtual void Get(const LookupKey& k, void* callback_args,
                   bool (*callback_func)(void* arg,
                                         const char* entry)) override {
    LockFreeSkipListRep::Iterator iter(&skip_list_);
    Slice dummy_slice;
    for (iter.Seek(dummy_slice, k.memtable_key().data());
         iter.Valid() && callback_func(callback_args, iter.key());
         iter.Next()) {
    }
  }

  virtual ~LockFreeSkipListRep() override {}

  // Iteration over the contents of a skip list
  class Iterator : public MemTableRep::Iterator {
    LockFreeSkipList<char*, const MemTableRep::KeyComparator&>::Iterator iter_;

   public:
    // Initialize an iterator over the specified list.
    // The returned iterator is not valid.
    explicit Iterator(
        LockFreeSkipList<char*, const MemTableRep::KeyComparator&>* list)
        : iter_(list) {}

    virtual ~Iterator() override {}

    // Returns true iff the iterator is positioned at a valid node.
    virtual bool Valid() const override { return iter_.Valid(); }

    // Returns the key at the current position.
    // REQUIRES: Valid()
    virtual const char* key() const override { return iter_.item(); }

    // Advances to the next position.
    // REQUIRES: Valid()
    virtual void Next() override { iter_.Next(); }

    // Advances to the previous position.
    // REQUIRES: Valid()
    virtual void Prev() override { iter_.Prev(); }

    // Advance to the first entry with a key >= target
    virtual void Seek(const Slice& user_key,
                      const char* memtable_key) override {
      if (memtable_key != nullptr) {
        iter_.Seek((char* const)memtable_key);
      } else {
        iter_.Seek((char* const)EncodeKey(&tmp_, user_key));
      }
    }

    // Position at the first entry in list.
    // Final state of iterator is Valid() iff list is not empty.
    virtual void SeekToFirst() override { iter_.SeekToFirst(); }

    // Position at the last entry in list.
    // Final state of iterator is Valid() iff list is not empty.
    virtual void SeekToLast() override { iter_.SeekToLast(); }

   protected:
    std::string tmp_;  // For passing to EncodeKey
  };

  virtual MemTableRep::Iterator* GetIterator(Arena* arena = nullptr) override {
    if (arena == nullptr) {
      return new LockFreeSkipListRep::Iterator(&skip_list_);
    } else {
      auto mem = arena->AllocateAligned(sizeof(LockFreeSkipListRep::Iterator));
      return new (mem) LockFreeSkipListRep::Iterator(&skip_list_);
    }
  }
};
}

MemTableRep* LockFreeSkipListFactory::CreateMemTableRep(
    const MemTableRep::KeyComparator& compare, MemTableAllocator* allocator,
    const SliceTransform*, Logger* logger) {
  return new LockFreeSkipListRep(compare, allocator);
}

}  // namespace rocksdb
