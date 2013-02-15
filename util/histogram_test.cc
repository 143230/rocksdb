#include "util/histogram.h"

#include "util/testharness.h"

namespace leveldb {

class HistogramTest { };

TEST(HistogramTest, BasicOperation) {

  Histogram histogram;
  for (uint64_t i = 1; i <= 100; i++) {
    histogram.Add(i);
  }

  {
    double median = histogram.Median();
    // ASSERT_LE(median, 50);
    ASSERT_GT(median, 0);
  }

  {
    double percentile100 = histogram.Percentile(100.0);
    ASSERT_LE(percentile100, 100.0);
    ASSERT_GT(percentile100, 0.0);
    double percentile99 = histogram.Percentile(99.0);
    double percentile85 = histogram.Percentile(85.0);
    ASSERT_LE(percentile99, 99.0);
    ASSERT_TRUE(percentile99 >= percentile85);
  }

  ASSERT_EQ(histogram.Average(), 50.5); // avg is acurately caluclated.
}

TEST(HistogramTest, EmptyHistogram) {
  Histogram histogram;
  ASSERT_EQ(histogram.Median(), 0.0);
  ASSERT_EQ(histogram.Percentile(85.0), 0.0);
  ASSERT_EQ(histogram.Average(), 0.0);
}

TEST(HistogramTest, ClearHistogram) {
  Histogram histogram;
  for (uint64_t i = 1; i <= 100; i++) {
    histogram.Add(i);
  }
  histogram.Clear();
  ASSERT_EQ(histogram.Median(), 0);
  ASSERT_EQ(histogram.Percentile(85.0), 0);
  ASSERT_EQ(histogram.Average(), 0);
}

}  // namespace leveldb

int main(int argc, char** argv) {
  return leveldb::test::RunAllTests();
}
