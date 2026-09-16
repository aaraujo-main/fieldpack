#include "fieldpack/registry.hpp"

#include <stdexcept>

namespace fieldpack {

Registry& Registry::instance() {
    static Registry registry;
    return registry;
}

std::uint32_t Registry::registerAnonymous(std::vector<FieldOps> fields, bool optimize) {
    return add(std::move(fields), optimize);
}

std::uint32_t Registry::registerNamed(std::string name, std::vector<FieldOps> fields, bool optimize,
                                      Redeclaration policy) {
    if (name.empty()) throw std::invalid_argument("schema name must not be empty");
    return add(std::move(fields), optimize, std::move(name), policy);
}

namespace {
bool equivalent(const Schema& schema, const std::vector<FieldOps>& fields, bool optimize) {
    if (schema.optimize() != optimize || schema.fieldCount() != fields.size()) return false;
    for (std::size_t index = 0; index < fields.size(); ++index) {
        const FieldOps& existing = schema.field(index);
        const FieldOps& candidate = fields[index];
        if (existing.type != candidate.type || existing.size != candidate.size ||
            existing.alignment != candidate.alignment || existing.rawCopySafe != candidate.rawCopySafe ||
            existing.construct != candidate.construct || existing.copy != candidate.copy ||
            existing.copyConstruct != candidate.copyConstruct || existing.destroy != candidate.destroy) {
            return false;
        }
        if (static_cast<bool>(existing.nested) != static_cast<bool>(candidate.nested)) return false;
        if (existing.nested &&
            (existing.nested->kind != candidate.nested->kind ||
             existing.nested->schemaId != candidate.nested->schemaId)) {
            return false;
        }
    }
    return true;
}
} // namespace

std::uint32_t Registry::add(std::vector<FieldOps> fields, bool optimize, std::string name,
                            Redeclaration policy) {
    if (!name.empty()) {
        const auto existing = names_.find(name);
        if (existing != names_.end()) {
            const Schema& schema = *schemas_[existing->second];
            if (equivalent(schema, fields, optimize)) return existing->second;
            if (policy == Redeclaration::Reject) {
                throw std::invalid_argument("schema name redeclared with different definition");
            }
        }
    }
    const std::uint32_t id = static_cast<std::uint32_t>(schemas_.size());
    schemas_.push_back(std::make_unique<Schema>(id, std::move(fields), optimize, name));
    if (!name.empty()) names_[name] = id;
    return id;
}

std::uint32_t Registry::addDerived(std::uint32_t baseId, std::vector<FieldOps> fields, bool optimize,
                                   std::string name, Redeclaration policy) {
    const Schema& base = get(baseId);
    std::vector<FieldOps> combined;
    combined.reserve(base.fieldCount() + fields.size());
    for (std::size_t index = 0; index < base.fieldCount(); ++index) {
        combined.push_back(base.field(index));
    }
    combined.insert(combined.end(), std::make_move_iterator(fields.begin()),
                    std::make_move_iterator(fields.end()));
    return add(std::move(combined), optimize, std::move(name), policy);
}

std::uint32_t Registry::deriveAnonymous(std::uint32_t baseId, std::vector<FieldOps> fields,
                                        bool optimize) {
    return addDerived(baseId, std::move(fields), optimize);
}

std::uint32_t Registry::deriveNamed(std::string name, std::uint32_t baseId,
                                    std::vector<FieldOps> fields, bool optimize,
                                    Redeclaration policy) {
    if (name.empty()) throw std::invalid_argument("schema name must not be empty");
    return addDerived(baseId, std::move(fields), optimize, std::move(name), policy);
}

std::optional<std::uint32_t> Registry::find(std::string_view name) const {
    const auto existing = names_.find(std::string(name));
    if (existing == names_.end()) return std::nullopt;
    return existing->second;
}

const Schema& Registry::get(std::uint32_t id) const {
    if (id >= schemas_.size()) throw std::out_of_range("schema id out of range");
    return *schemas_[id];
}

} // namespace fieldpack