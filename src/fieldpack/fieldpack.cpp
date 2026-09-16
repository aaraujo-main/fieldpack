#include "fieldpack/fieldpack.hpp"

#include <cstring>
#include <new>
#include <stdexcept>

namespace fieldpack {

namespace {
std::uint32_t readSchemaId(const std::uint8_t* storage) noexcept {
    std::uint32_t schemaId = 0;
    std::memcpy(&schemaId, storage, sizeof(schemaId));
    return schemaId;
}

void constructFields(std::uint32_t schemaId, std::uint8_t* storage) {
    const Schema& definition = Registry::instance().get(schemaId);
    std::memcpy(storage, &schemaId, sizeof(schemaId));
    std::size_t constructed = 0;
    try {
        for (; constructed < definition.nonTrivialFields().size(); ++constructed) {
            const std::size_t index = definition.nonTrivialFields()[constructed];
            const FieldOps& ops = definition.field(index);
            ops.construct(storage + definition.offset(index), ops);
        }
    } catch (...) {
        while (constructed > 0) {
            --constructed;
            const std::size_t index = definition.nonTrivialFields()[constructed];
            const FieldOps& ops = definition.field(index);
            ops.destroy(storage + definition.offset(index), ops);
        }
        throw;
    }
}

void destroyFields(std::uint32_t schemaId, std::uint8_t* storage) noexcept {
    const Schema& definition = Registry::instance().get(schemaId);
    for (std::size_t position = definition.nonTrivialFields().size(); position-- > 0;) {
        const std::size_t index = definition.nonTrivialFields()[position];
        const FieldOps& ops = definition.field(index);
        ops.destroy(storage + definition.offset(index), ops);
    }
}

void copyFields(std::uint32_t schemaId, const std::uint8_t* source, std::uint8_t* destination) {
    const Schema& definition = Registry::instance().get(schemaId);
    std::memcpy(destination, source, sizeof(schemaId));
    for (std::size_t index = 0; index < definition.fieldCount(); ++index) {
        const FieldOps& ops = definition.field(index);
        if (ops.rawCopySafe) {
            std::memcpy(destination + definition.offset(index), source + definition.offset(index),
                        ops.size);
        }
    }
    std::size_t constructed = 0;
    try {
        for (; constructed < definition.nonTrivialFields().size(); ++constructed) {
            const std::size_t index = definition.nonTrivialFields()[constructed];
            const FieldOps& ops = definition.field(index);
            ops.copyConstruct(source + definition.offset(index), destination + definition.offset(index),
                              ops.size, ops);
        }
    } catch (...) {
        while (constructed > 0) {
            --constructed;
            const std::size_t index = definition.nonTrivialFields()[constructed];
            const FieldOps& ops = definition.field(index);
            ops.destroy(destination + definition.offset(index), ops);
        }
        throw;
    }
}

void constructSlice(void* address, const FieldOps& ops) {
    constructFields(ops.nested->schemaId, static_cast<std::uint8_t*>(address));
}

void copySlice(const void* source, void* destination, std::size_t, const FieldOps& ops) {
    destroyFields(ops.nested->schemaId, static_cast<std::uint8_t*>(destination));
    copyFields(ops.nested->schemaId, static_cast<const std::uint8_t*>(source),
               static_cast<std::uint8_t*>(destination));
}

void copyConstructSlice(const void* source, void* destination, std::size_t, const FieldOps& ops) {
    copyFields(ops.nested->schemaId, static_cast<const std::uint8_t*>(source),
               static_cast<std::uint8_t*>(destination));
}

void destroySlice(void* address, const FieldOps& ops) noexcept {
    destroyFields(ops.nested->schemaId, static_cast<std::uint8_t*>(address));
}

void constructPack(void* address, const FieldOps& ops) {
    new (address) FieldPack(ops.nested->schemaId);
}

void copyPack(const void* source, void* destination, std::size_t, const FieldOps&) {
    *static_cast<FieldPack*>(destination) = *static_cast<const FieldPack*>(source);
}

void copyConstructPack(const void* source, void* destination, std::size_t, const FieldOps&) {
    new (destination) FieldPack(*static_cast<const FieldPack*>(source));
}

void destroyPack(void* address, const FieldOps&) noexcept {
    static_cast<FieldPack*>(address)->~FieldPack();
}
} // namespace

FieldPackSlice::FieldPackSlice(void* storage) : storage_(static_cast<std::uint8_t*>(storage)) {}

std::uint32_t FieldPackSlice::schemaId() const noexcept { return readSchemaId(storage_); }
const Schema& FieldPackSlice::schema() const { return Registry::instance().get(schemaId()); }
void* FieldPackSlice::fieldAddress(std::size_t logicalIndex) {
    return storage_ + schema().offset(logicalIndex);
}
const void* FieldPackSlice::fieldAddress(std::size_t logicalIndex) const {
    return storage_ + schema().offset(logicalIndex);
}
void FieldPackSlice::rebind(void* storage) { storage_ = static_cast<std::uint8_t*>(storage); }

FieldPack::FieldPack(std::uint32_t schemaId) : storage_(nullptr) {
    const Schema& definition = Registry::instance().get(schemaId);
    storage_ = new std::uint8_t[definition.totalSize()];
    try {
        constructAll(schemaId);
    } catch (...) {
        delete[] storage_;
        storage_ = nullptr;
        throw;
    }
}

void FieldPack::constructAll(std::uint32_t schemaId) {
    constructFields(schemaId, storage_);
}

FieldPack::FieldPack(const FieldPack& other)
    : storage_(nullptr) {
    if (!other.storage_) throw std::logic_error("cannot copy moved-from FieldPack");
    const std::uint32_t schemaId = other.schemaId();
    const Schema& definition = Registry::instance().get(schemaId);
    storage_ = new std::uint8_t[definition.totalSize()];
    try {
        copyFields(schemaId, other.storage_, storage_);
    } catch (...) {
        delete[] storage_;
        storage_ = nullptr;
        throw;
    }
}

FieldPack& FieldPack::operator=(const FieldPack& other) {
    if (this != &other) {
        // Moved-from packs have no storage; validate before trivial raw-copy fast path.
        if (!other.storage_) throw std::logic_error("cannot copy moved-from FieldPack");
        const std::uint32_t otherSchemaId = other.schemaId();
        if (storage_ && schemaId() == otherSchemaId &&
            Registry::instance().get(schemaId()).nonTrivialFields().empty()) {
            const Schema& definition = Registry::instance().get(schemaId());
            // Same-schema raw fields are already live-by-representation; avoid allocation
            // and copy-and-move overhead while preserving non-trivial assignment safety.
            std::memcpy(storage_, other.storage_, definition.totalSize());
            return *this;
        }
        FieldPack replacement(other);
        *this = std::move(replacement);
    }
    return *this;
}

FieldPack::FieldPack(FieldPack&& other) noexcept
    : storage_(other.storage_) {
    other.storage_ = nullptr;
}

FieldPack& FieldPack::operator=(FieldPack&& other) noexcept {
    if (this != &other) {
        release();
        storage_ = other.storage_;
        other.storage_ = nullptr;
    }
    return *this;
}

FieldPack::~FieldPack() { release(); }

void FieldPack::release() noexcept {
    if (!storage_) return;
    destroyFields(schemaId(), storage_);
    delete[] storage_;
    storage_ = nullptr;
}

std::uint32_t FieldPack::schemaId() const noexcept {
    return storage_ ? readSchemaId(storage_) : 0;
}
const Schema& FieldPack::schema() const { return Registry::instance().get(schemaId()); }
void* FieldPack::fieldAddress(std::size_t logicalIndex) {
    return storage_ + schema().offset(logicalIndex);
}
const void* FieldPack::fieldAddress(std::size_t logicalIndex) const {
    return storage_ + schema().offset(logicalIndex);
}

FieldOps makeSliceField(std::uint32_t schemaId) {
    const Schema& schema = Registry::instance().get(schemaId);
    return {"slice", schema.totalSize(), alignof(std::max_align_t), false,
            &constructSlice, &copySlice, &copyConstructSlice, &destroySlice,
            std::make_shared<NestedField>(NestedField{NestedKind::Slice, schemaId})};
}

FieldOps makePackField(std::uint32_t schemaId) {
    Registry::instance().get(schemaId);
    return {"pack", sizeof(FieldPack), alignof(FieldPack), false, &constructPack, &copyPack,
            &copyConstructPack, &destroyPack,
            std::make_shared<NestedField>(NestedField{NestedKind::Pack, schemaId})};
}

} // namespace fieldpack
