#pragma once

#include "fieldpack/field_ops.hpp"
#include "fieldpack/registry.hpp"
#include "fieldpack/schema.hpp"

#include <cstddef>
#include <cstdint>

namespace fieldpack {

class FieldPackSlice {
public:
    explicit FieldPackSlice(void* storage);
    FieldPackSlice(const FieldPackSlice&) = default;
    FieldPackSlice& operator=(const FieldPackSlice&) = default;
    ~FieldPackSlice() = default;

    std::uint32_t schemaId() const noexcept;
    const Schema& schema() const;
    void* fieldAddress(std::size_t logicalIndex);
    const void* fieldAddress(std::size_t logicalIndex) const;
    void rebind(void* storage);

private:
    std::uint8_t* storage_;
};

/**
 * @brief Owns one schema-defined byte buffer and manages every field lifetime.
 *
 * Copies deep-copy fields through their callbacks; moves transfer buffer
 * ownership without moving individual fields.
 */
class FieldPack {
public:
    explicit FieldPack(std::uint32_t schemaId);
    FieldPack(const FieldPack& other);
    FieldPack& operator=(const FieldPack& other);
    FieldPack(FieldPack&& other) noexcept;
    FieldPack& operator=(FieldPack&& other) noexcept;
    ~FieldPack();

    std::uint32_t schemaId() const noexcept;
    const Schema& schema() const;
    void* fieldAddress(std::size_t logicalIndex);
    const void* fieldAddress(std::size_t logicalIndex) const;

private:
    void release() noexcept;
    void constructAll(std::uint32_t schemaId);
    std::uint8_t* storage_;
};

FieldOps makeSliceField(std::uint32_t schemaId);
FieldOps makePackField(std::uint32_t schemaId);

} // namespace fieldpack
