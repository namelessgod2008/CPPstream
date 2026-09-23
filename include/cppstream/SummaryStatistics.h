#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>

namespace cppstream {

/// A port of java.util.IntSummaryStatistics.
///
/// The three statistics classes exist because Java's primitive streams need to
/// carry primitive min/max/sum without boxing. Here they are ordinary value types
/// returned by Collectors::summarizingInt/Long/Double.
///
/// Empty-state sentinels are Java's, verbatim: min is the type's maximum and max
/// is its minimum, so the first accept() always wins. Java reports them as-is
/// through getMin()/getMax(); that behaviour is kept rather than "fixed".
class IntSummaryStatistics {
public:
    /// Java: IntSummaryStatistics.accept(value).
    void accept(int value) noexcept {
        ++count_;
        sum_ += value;
        min_ = std::min(value, min_);
        max_ = std::max(value, max_);
    }

    /// Java: IntSummaryStatistics.combine(other).
    void combine(const IntSummaryStatistics& other) noexcept {
        count_ += other.count_;
        sum_ += other.sum_;
        min_ = std::min(other.min_, min_);
        max_ = std::max(other.max_, max_);
    }

    [[nodiscard]] std::int64_t getCount() const noexcept { return count_; }
    [[nodiscard]] std::int64_t getSum() const noexcept { return sum_; }
    [[nodiscard]] int getMin() const noexcept { return min_; }
    [[nodiscard]] int getMax() const noexcept { return max_; }

    /// Java: getAverage(), which is 0.0 for an empty set rather than NaN.
    [[nodiscard]] double getAverage() const noexcept {
        return count_ == 0 ? 0.0 : static_cast<double>(sum_) / static_cast<double>(count_);
    }

    [[nodiscard]] std::string toString() const {
        return "IntSummaryStatistics{count=" + std::to_string(count_) + ", sum=" + std::to_string(sum_) +
               ", min=" + std::to_string(min_) + ", average=" + std::to_string(getAverage()) +
               ", max=" + std::to_string(max_) + "}";
    }

private:
    std::int64_t count_ = 0;
    std::int64_t sum_ = 0;
    int min_ = std::numeric_limits<int>::max();
    int max_ = std::numeric_limits<int>::min();
};

/// A port of java.util.LongSummaryStatistics.
class LongSummaryStatistics {
public:
    void accept(std::int64_t value) noexcept {
        ++count_;
        sum_ += value;
        min_ = std::min(value, min_);
        max_ = std::max(value, max_);
    }

    void combine(const LongSummaryStatistics& other) noexcept {
        count_ += other.count_;
        sum_ += other.sum_;
        min_ = std::min(other.min_, min_);
        max_ = std::max(other.max_, max_);
    }

    [[nodiscard]] std::int64_t getCount() const noexcept { return count_; }
    [[nodiscard]] std::int64_t getSum() const noexcept { return sum_; }
    [[nodiscard]] std::int64_t getMin() const noexcept { return min_; }
    [[nodiscard]] std::int64_t getMax() const noexcept { return max_; }

    [[nodiscard]] double getAverage() const noexcept {
        return count_ == 0 ? 0.0 : static_cast<double>(sum_) / static_cast<double>(count_);
    }

    [[nodiscard]] std::string toString() const {
        return "LongSummaryStatistics{count=" + std::to_string(count_) + ", sum=" + std::to_string(sum_) +
               ", min=" + std::to_string(min_) + ", average=" + std::to_string(getAverage()) +
               ", max=" + std::to_string(max_) + "}";
    }

private:
    std::int64_t count_ = 0;
    std::int64_t sum_ = 0;
    std::int64_t min_ = std::numeric_limits<std::int64_t>::max();
    std::int64_t max_ = std::numeric_limits<std::int64_t>::min();
};

/// A port of java.util.DoubleSummaryStatistics.
///
/// Unlike the integral variants above, min and max are compared with plain `<` and
/// `>` rather than std::min/std::max: Java's DoubleSummaryStatistics.accept only
/// tracks a value when `value < min` / `value > max`, so a NaN never becomes the
/// minimum or the maximum (it only poisons the sum). std::min/std::max return the
/// NaN instead, which would diverge from the API being mirrored.
///
/// Java's version uses Kahan compensated summation internally and lets getSum()
/// disagree slightly with the naive sum. This keeps the naive sum: correctness of
/// the API matters more here than the last bit of the mantissa, and pretending to
/// compensate without Java's exact algorithm would be a worse lie.
class DoubleSummaryStatistics {
public:
    void accept(double value) noexcept {
        ++count_;
        sum_ += value;
        // NOLINTNEXTLINE(readability-use-std-min-max): a NaN must not win, see above.
        if (value < min_) {
            min_ = value;
        }
        // NOLINTNEXTLINE(readability-use-std-min-max): a NaN must not win, see above.
        if (value > max_) {
            max_ = value;
        }
    }

    void combine(const DoubleSummaryStatistics& other) noexcept {
        count_ += other.count_;
        sum_ += other.sum_;
        // NOLINTNEXTLINE(readability-use-std-min-max): a NaN must not win, see above.
        if (other.min_ < min_) {
            min_ = other.min_;
        }
        // NOLINTNEXTLINE(readability-use-std-min-max): a NaN must not win, see above.
        if (other.max_ > max_) {
            max_ = other.max_;
        }
    }

    [[nodiscard]] std::int64_t getCount() const noexcept { return count_; }
    [[nodiscard]] double getSum() const noexcept { return sum_; }
    [[nodiscard]] double getMin() const noexcept { return min_; }
    [[nodiscard]] double getMax() const noexcept { return max_; }

    /// Java: NaN for an empty set (0/0), unlike the integral variants.
    [[nodiscard]] double getAverage() const noexcept {
        return count_ == 0 ? 0.0 : sum_ / static_cast<double>(count_);
    }

    [[nodiscard]] std::string toString() const {
        return "DoubleSummaryStatistics{count=" + std::to_string(count_) + ", sum=" + std::to_string(sum_) +
               ", min=" + std::to_string(min_) + ", average=" + std::to_string(getAverage()) +
               ", max=" + std::to_string(max_) + "}";
    }

private:
    std::int64_t count_ = 0;
    double sum_ = 0.0;
    double min_ = std::numeric_limits<double>::infinity();
    double max_ = -std::numeric_limits<double>::infinity();
};

}  // namespace cppstream
