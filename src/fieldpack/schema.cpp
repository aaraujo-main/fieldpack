#include "fieldpack/schema.hpp"

#include <algorithm>
#include <cstddef>
#include <stdexcept>

namespace fieldpack {

Schema::Schema(std::uint32_t id, std::vector<FieldOps> fields, bool optimize, std::string name)
        : id_(id), name_(std::move(name)), optimize_(optimize), totalSize_(sizeof(std::uint32_t)),
            fields_(std::move(fields)) {
    // Offset bitmask rounding and byte-array allocation require supported power-of-two alignment.
    for (const FieldOps& ops : fields_) {
        if (ops.alignment == 0 || (ops.alignment & (ops.alignment - 1)) != 0 ||
            ops.alignment > alignof(std::max_align_t)) {
            throw std::invalid_argument("field alignment is unsupported");
        }
    }
    std::vector<std::size_t> order(fields_.size());
    for (std::size_t index = 0; index < order.size(); ++index) order[index] = index;
    if (optimize) {
        // Sort physical placement by alignment while offsets preserve logical indexes.
        std::stable_sort(order.begin(), order.end(), [this](std::size_t left, std::size_t right) {
            return fields_[left].alignment > fields_[right].alignment;
        });
    }

    offsets_.resize(fields_.size());
    nonTrivialFields_.reserve(fields_.size());
    for (std::size_t physical = 0; physical < order.size(); ++physical) {
        const FieldOps& ops = fields_[order[physical]];
        totalSize_ = (totalSize_ + ops.alignment - 1) & ~(ops.alignment - 1);
        offsets_[order[physical]] = totalSize_;
        totalSize_ += ops.size;
    }
    for (std::size_t index = 0; index < fields_.size(); ++index) {
        if (!fields_[index].rawCopySafe) nonTrivialFields_.push_back(index);
    }
    totalSize_ = (totalSize_ + 7) & ~std::size_t(7);
}

std::uint32_t Schema::id() const noexcept { return id_; }
const std::string& Schema::name() const noexcept { return name_; }
bool Schema::optimize() const noexcept { return optimize_; }
std::size_t Schema::fieldCount() const noexcept { return fields_.size(); }
std::size_t Schema::totalSize() const noexcept { return totalSize_; }
const std::vector<std::size_t>& Schema::nonTrivialFields() const noexcept {
    return nonTrivialFields_;
}

std::size_t Schema::offset(std::size_t logicalIndex) const {
    if (logicalIndex >= offsets_.size()) throw std::out_of_range("field index out of range");
    return offsets_[logicalIndex];
}

const FieldOps& Schema::field(std::size_t logicalIndex) const {
    if (logicalIndex >= fields_.size()) throw std::out_of_range("field index out of range");
    return fields_[logicalIndex];
}

} // namespace fieldpack