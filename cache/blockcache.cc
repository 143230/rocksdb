#include <utility>

#include "cache/blockcache.h"

using namespace rocksdb;

//
// BlockCacheImpl
//
Status BlockCacheImpl::Open() {
  Status status;

  WriteLock _(&lock_);

  assert(!size_);

  //
  // Create directory
  //
  status = opt_.env->CreateDirIfMissing(opt_.path);
  if (!status.ok()) {
    Error(opt_.log, "Error creating directory %s. %s", opt_.path.c_str(),
          status.ToString().c_str());
    return status;
  }

  //
  // Create directory
  //
  status = opt_.env->CreateDirIfMissing(GetCachePath());
  if (!status.ok()) {
    Error(opt_.log, "Error creating directory %s. %s",
          GetCachePath().c_str(), status.ToString().c_str());
    return status;
  }

  Info(opt_.log, "Resetting directory %s", opt_.path.c_str());

  assert(!cacheFile_);
  NewCacheFile();
  assert(cacheFile_);

  return Status::OK();
}

Status BlockCacheImpl::Close() {
  // stop the insert thread
  InsertOp op(/*quit=*/ true);
  insert_ops_.Push(std::move(op));
  insert_th_.join();

  // stop the writer before
  writer_.Stop();

  // clear all metadata
  WriteLock _(&lock_);
  metadata_.Clear();
  return Status::OK();
}

Status BlockCacheImpl::Insert(const Slice& key, void* data, const size_t size) {
  // update stats
  stats_.bytes_pipelined_.Add(size);

  std::unique_ptr<char[]> tmp(new char[size]);
  memcpy(tmp.get(), data, size);

  if (opt_.pipeline_writes_) {
    // off load the write to the write thread
    insert_ops_.Push(InsertOp(std::move(key.ToString()), std::move(tmp), size));
    assert(!tmp);
    return Status::OK();
  }

  assert(!opt_.pipeline_writes_);
  return InsertImpl(key, tmp, size);
}

void BlockCacheImpl::InsertMain() {
  while (true) {
    InsertOp op(std::move(insert_ops_.Pop()));

    if (op.exit_loop_) {
      break;
    }

    while (!InsertImpl(Slice(op.key_), op.data_, op.size_).ok());
  }
}

Status BlockCacheImpl::InsertImpl(const Slice& key,
                                  const std::unique_ptr<char[]>& buf,
                                  const size_t size) {
  // pre-condition
  assert(buf);
  assert(size);
  assert(cacheFile_);

  WriteLock _(&lock_);

  LBA lba;
  if (metadata_.Lookup(key, &lba)) {
    // the key already exisits, this is duplicate insert
    return Status::OK();
  }

  Slice data(buf.get(), size);
  while (!cacheFile_->Append(key, data, &lba)) {
    if (!cacheFile_->Eof()) {
      // add condition variable driven logic here
      Debug(opt_.log, "Error inserting to cache file %d", cacheFile_->cacheid());
      return Status::TryAgain();
    }

    assert(cacheFile_->Eof());
    NewCacheFile();
  }

  // Insert into lookup index
  BlockInfo* info = new BlockInfo(key, lba);
  cacheFile_->Add(info);
  bool status = metadata_.Insert(info);
  (void) status;
  assert(status);

  // update stats
  stats_.bytes_written_.Add(size);

  return Status::OK();
}

bool BlockCacheImpl::Lookup(const Slice& key, unique_ptr<char[]>* val,
                            size_t* size)
{
  // ReadLock _(&lock_);

  LBA lba;
  bool status;
  status = metadata_.Lookup(key, &lba);
  if (!status) {
    Info(opt_.log, "Error looking up index for key %s", key.ToString().c_str());
    return status;
  }

  BlockCacheFile* const file = metadata_.Lookup(lba.cache_id_);
  if (!file) {
    // this can happen because the block index and cache file index are
    // different, and the cache file might be removed between the two lookups
    stats_.cache_misses_++;
    return false;
  }

  assert(file->refs_);

  unique_ptr<char[]> scratch(new char[lba.size_]);
  Slice blk_key;
  Slice blk_val;

  status = file->Read(lba, &blk_key, &blk_val, scratch.get());
  --file->refs_;
  if (!status) {
    assert(!"Unexpected error looking up cache");
    stats_.cache_misses_++;
    return false;
  }

  assert(blk_key == key);

  val->reset(new char[blk_val.size()]);
  memcpy(val->get(), blk_val.data(), blk_val.size());
  *size = blk_val.size();

  stats_.bytes_read_.Add(*size);
  stats_.cache_hits_++;

  return true;
}

bool BlockCacheImpl::Erase(const Slice& key) {
  WriteLock _(&lock_);
  BlockInfo* info = metadata_.Remove(key);
  assert(info);
  delete info;
  return true;
}

void BlockCacheImpl::NewCacheFile() {
  lock_.AssertHeld();

  Info(opt_.log, "Creating cache file %d", writerCacheId_);

  writerCacheId_++;

  cacheFile_ = new WriteableCacheFile(opt_.env, bufferAllocator_, writer_,
                                      GetCachePath(), writerCacheId_,
                                      opt_.cache_file_size, opt_.log);
  bool status;
  status = cacheFile_->Create();
  assert(status);

  // insert to cache files tree
  status = metadata_.Insert(cacheFile_);
  (void) status;
  assert(status);
}

bool BlockCacheImpl::Reserve(const size_t size) {
  WriteLock _(&lock_);
  assert(size_ <= opt_.cache_size);

  if (size + size_ <= opt_.cache_size) {
    // there is enough space to write
    size_ += size;
    return true;
  }

  assert(size + size_ >= opt_.cache_size);
  // there is not enough space to fit the requested data
  // we can clear some space by evicting cold data

  while (size + size_ > opt_.cache_size * 0.9) {
    unique_ptr<BlockCacheFile> f(metadata_.Evict());
    if (!f) {
      // nothing is evictable
      return false;
    }
    assert(!f->refs_);
    size_t file_size;
    if (!f->Delete(&file_size).ok()) {
      // unable to delete file
      return false;
    }

    assert(file_size <= size_);
    size_ -= file_size;
  }

  size_ += size;
  assert(size_ <= opt_.cache_size * 0.9);
  return true;
}
