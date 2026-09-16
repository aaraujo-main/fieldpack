#include "fieldpack/field_ops.hpp"

#include <cstring>

namespace fieldpack {

namespace {
void trivialConstruct(void*, const FieldOps&) {}

void trivialCopy(const void* source, void* destination, std::size_t size, const FieldOps&) {
    std::memcpy(destination, source, size);
}

void trivialDestroy(void*, const FieldOps&) noexcept {}
} // namespace

FieldOps makeTrivialField(std::string type, std::size_t size, std::size_t alignment) {
    return {std::move(type), size, alignment, true, &trivialConstruct, &trivialCopy,
            nullptr, &trivialDestroy, nullptr};
}

} // namespace fieldpack