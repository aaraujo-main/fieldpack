#pragma once

#include "fieldpack/field_ops.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace fieldpack {

/**
 * @brief Immutable memory layout and field metadata for one FieldPack schema.
 *
 * Logical field indexes remain stable even when physical placement is optimized
 * by alignment.
 */
class Schema {
public:
    Schema(std::uint32_t id, std::vector<FieldOps> fields, bool optimize, std::string name);

    std::uint32_t id() const noexcept;
    const std::string& name() const noexcept;
    bool optimize() const noexcept;
    std::size_t fieldCount() const noexcept;
    std::size_t totalSize() const noexcept;
    const std::vector<std::size_t>& nonTrivialFields() const noexcept;
    std::size_t offset(std::size_t logicalIndex) const;
    const FieldOps& field(std::size_t logicalIndex) const;

private:
    std::uint32_t id_;
    std::string name_;
    bool optimize_;
    std::size_t totalSize_;
    std::vector<FieldOps> fields_;
    std::vector<std::size_t> offsets_;
    std::vector<std::size_t> nonTrivialFields_;
};

} // namespace fieldpack