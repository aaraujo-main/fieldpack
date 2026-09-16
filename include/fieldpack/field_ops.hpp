#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <string>
#include <utility>

namespace fieldpack {

struct FieldOps;

enum class NestedKind { None, Slice, Pack };

struct NestedField {
    NestedKind kind;
    std::uint32_t schemaId;
};

using Constructor = void (*)(void*, const FieldOps&);
using Copier = void (*)(const void*, void*, std::size_t, const FieldOps&);
using CopyConstructor = void (*)(const void*, void*, std::size_t, const FieldOps&);
using Destructor = void (*)(void*, const FieldOps&) noexcept;

/** @brief Type-erased construction, copy, destruction, and layout metadata for one field. */
struct FieldOps {
    std::string type;
    std::size_t size;
    std::size_t alignment;
    bool rawCopySafe;
    Constructor construct;
    Copier copy;
    CopyConstructor copyConstruct;
    Destructor destroy;
    std::shared_ptr<const NestedField> nested;
};

template <typename T>
void construct(void* address, const FieldOps&) {
    new (address) T();
}

template <typename T>
void copy(const void* source, void* destination, std::size_t, const FieldOps&) {
    *static_cast<T*>(destination) = *static_cast<const T*>(source);
}

template <typename T>
void copyConstruct(const void* source, void* destination, std::size_t, const FieldOps&) {
    new (destination) T(*static_cast<const T*>(source));
}

template <typename T>
void destroy(void* address, const FieldOps&) noexcept {
    static_cast<T*>(address)->~T();
}

template <typename T>
FieldOps makeField(std::string type) {
    return {std::move(type), sizeof(T), alignof(T), false, &construct<T>, &copy<T>,
            &copyConstruct<T>, &destroy<T>, nullptr};
}

FieldOps makeTrivialField(std::string type, std::size_t size, std::size_t alignment);

} // namespace fieldpack