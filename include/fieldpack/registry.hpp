#pragma once

#include "fieldpack/schema.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fieldpack {

/**
 * @brief Process-wide owner and lookup service for registered schemas.
 *
 * Schema IDs are process-local and registered schemas remain alive for the
 * lifetime of the registry.
 */
class Registry {
public:
    enum class Redeclaration { ReuseEquivalent, Reject, Replace };

    static Registry& instance();
    std::uint32_t registerAnonymous(std::vector<FieldOps> fields, bool optimize);
    std::uint32_t registerNamed(std::string name, std::vector<FieldOps> fields, bool optimize,
                                Redeclaration policy = Redeclaration::ReuseEquivalent);
    std::uint32_t add(std::vector<FieldOps> fields, bool optimize, std::string name = {},
                      Redeclaration policy = Redeclaration::ReuseEquivalent);
    std::uint32_t deriveAnonymous(std::uint32_t baseId, std::vector<FieldOps> fields,
                                  bool optimize);
    std::uint32_t deriveNamed(std::string name, std::uint32_t baseId,
                              std::vector<FieldOps> fields, bool optimize,
                              Redeclaration policy = Redeclaration::ReuseEquivalent);
    std::uint32_t addDerived(std::uint32_t baseId, std::vector<FieldOps> fields, bool optimize,
                             std::string name = {},
                             Redeclaration policy = Redeclaration::ReuseEquivalent);
    std::optional<std::uint32_t> find(std::string_view name) const;
    const Schema& get(std::uint32_t id) const;

private:
    Registry() = default;
    std::vector<std::unique_ptr<Schema>> schemas_;
    std::unordered_map<std::string, std::uint32_t> names_;
};

} // namespace fieldpack