// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <cassert>
#include <math.h>
#include <stdio.h>
#include "port/port.h"
#include "util/histogram.h"

namespace leveldb {

HistogramBucketMapper::HistogramBucketMapper() :
  // Add newer bucket index here.
  // Should be alwyas added in sorted order.
  bucketValues_({
  1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 12, 14, 16, 18, 20, 25, 30, 35, 40, 45,
  50, 60, 70, 80, 90, 100, 120, 140, 160, 180, 200, 250, 300, 350, 400, 450,
  500, 600, 700, 800, 900, 1000, 1200, 1400, 1600, 1800, 2000, 2500, 3000,
  3500, 4000, 4500, 5000, 6000, 7000, 8000, 9000, 10000, 12000, 14000,
  16000, 18000, 20000, 25000, 30000, 35000, 40000, 45000, 50000, 60000,
  70000, 80000, 90000, 100000, 120000, 140000, 160000, 180000, 200000,
  250000, 300000, 350000, 400000, 450000, 500000, 600000, 700000, 800000,
  900000, 1000000, 1200000, 1400000, 1600000, 1800000, 2000000, 2500000,
  3000000, 3500000, 4000000, 4500000, 5000000, 6000000, 7000000, 8000000,
  9000000, 10000000, 12000000, 14000000, 16000000, 18000000, 20000000,
  25000000, 30000000, 35000000, 40000000, 45000000, 50000000, 60000000,
  70000000, 80000000, 90000000, 100000000, 120000000, 140000000, 160000000,
  180000000, 200000000, 250000000, 300000000, 350000000, 400000000,
  450000000, 500000000, 600000000, 700000000, 800000000, 900000000,
  1000000000}),
  maxBucketValue_(bucketValues_.back()),
  minBucketValue_(bucketValues_.front()) {
  for (size_t i =0; i < bucketValues_.size(); ++i) {
    valueIndexMap_[bucketValues_[i]] = i;
  }
}

const size_t HistogramBucketMapper::IndexForValue(const uint64_t value) const {
  if (value >= maxBucketValue_) {
    return bucketValues_.size() - 1;
  } else if ( value >= minBucketValue_ ) {
    std::map<uint64_t, uint64_t>::const_iterator lowerBound =
      valueIndexMap_.lower_bound(value);
    if (lowerBound != valueIndexMap_.end()) {
      return lowerBound->second;
    } else {
      return 0;
    }
  } else {
    return 0;
  }
}

namespace {
  const HistogramBucketMapper bucketMapper;
}


Histogram::Histogram() :
  min_(bucketMapper.LastValue()),
  max_(0),
  num_(0),
  sum_(0),
  sum_squares_(0),
  buckets_(std::vector<uint64_t>(bucketMapper.BucketCount(), 0)) {}

void Histogram::Clear() {
  min_ = bucketMapper.LastValue();
  max_ = 0;
  num_ = 0;
  sum_ = 0;
  sum_squares_ = 0;
  buckets_.resize(bucketMapper.BucketCount(), 0);
}

void Histogram::Add(uint64_t value) {
  const size_t index = bucketMapper.IndexForValue(value);
  buckets_[index] += 1;
  if (min_ > value) min_ = value;
  if (max_ < value) max_ = value;
  num_++;
  sum_ += value;
  sum_squares_ += (value * value);
}

void Histogram::Add(double value) {
  Add(static_cast<uint64_t>(value));
}

void Histogram::Merge(const Histogram& other) {
  if (other.min_ < min_) min_ = other.min_;
  if (other.max_ > max_) max_ = other.max_;
  num_ += other.num_;
  sum_ += other.sum_;
  sum_squares_ += other.sum_squares_;
  for (int b = 0; b < bucketMapper.BucketCount(); b++) {
    buckets_[b] += other.buckets_[b];
  }
}

double Histogram::Median() const {
  return Percentile(50.0);
}

double Histogram::Percentile(double p) const {
  double threshold = num_ * (p / 100.0);
  double sum = 0;
  for (int b = 0; b < bucketMapper.BucketCount(); b++) {
    sum += buckets_[b];
    if (sum >= threshold) {
      // Scale linearly within this bucket
      double left_point = (b == 0) ? 0 : bucketMapper.BucketLimit(b-1);
      double right_point = bucketMapper.BucketLimit(b);
      double left_sum = sum - buckets_[b];
      double right_sum = sum;
      double pos = 0;
      double right_left_diff = right_sum - left_sum;
      if (right_left_diff != 0) {
       pos = (threshold - left_sum) / (right_sum - left_sum);
      }
      double r = left_point + (right_point - left_point) * pos;
      if (r < min_) r = min_;
      if (r > max_) r = max_;
      return r;
    }
  }
  return max_;
}

double Histogram::Average() const {
  if (num_ == 0.0) return 0;
  return sum_ / num_;
}

double Histogram::StandardDeviation() const {
  if (num_ == 0.0) return 0;
  double variance = (sum_squares_ * num_ - sum_ * sum_) / (num_ * num_);
  return sqrt(variance);
}

std::string Histogram::ToString() const {
  std::string r;
  char buf[200];
  snprintf(buf, sizeof(buf),
           "Count: %.0f  Average: %.4f  StdDev: %.2f\n",
           num_, Average(), StandardDeviation());
  r.append(buf);
  snprintf(buf, sizeof(buf),
           "Min: %.4f  Median: %.4f  Max: %.4f\n",
           (num_ == 0.0 ? 0.0 : min_), Median(), max_);
  r.append(buf);
  r.append("------------------------------------------------------\n");
  const double mult = 100.0 / num_;
  double sum = 0;
  for (int b = 0; b < bucketMapper.BucketCount(); b++) {
    if (buckets_[b] <= 0.0) continue;
    sum += buckets_[b];
    snprintf(buf, sizeof(buf),
             "[ %ld, %ld ) %ld %7.3f%% %7.3f%% ",
             ((b == 0) ? 0 : bucketMapper.BucketLimit(b-1)),      // left
             bucketMapper.BucketLimit(b),                           // right
             buckets_[b],                             // count
             mult * buckets_[b],                      // percentage
             mult * sum);                             // cumulative percentage
    r.append(buf);

    // Add hash marks based on percentage; 20 marks for 100%.
    int marks = static_cast<int>(20*(buckets_[b] / num_) + 0.5);
    r.append(marks, '#');
    r.push_back('\n');
  }
  return r;
}

}  // namespace leveldb
