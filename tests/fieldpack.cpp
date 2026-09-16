#include "fieldpack/fieldpack.hpp"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

template <typename Operation>
void expectOutOfRange(const Operation& operation) {
    bool threw = false;
    try {
        operation();
    } catch (const std::out_of_range&) {
        threw = true;
    }
    assert(threw);
}

} // namespace

int main() {
    using fieldpack::FieldOps;
    using fieldpack::FieldPack;
    using fieldpack::FieldPackSlice;
    using fieldpack::Registry;

    // Global schema registry and flat layout metadata.
    std::vector<FieldOps> fields;
    fields.push_back(fieldpack::makeTrivialField("int", sizeof(std::int32_t), alignof(std::int32_t)));
    fields.push_back(fieldpack::makeTrivialField("double", sizeof(double), alignof(double)));
    fields.push_back(fieldpack::makeField<std::string>("string"));

    const std::uint32_t schemaId = Registry::instance().add(fields, true);
    const auto& schema = Registry::instance().get(schemaId);
    assert(schema.id() == schemaId);
    assert(schema.name().empty());
    assert(schema.fieldCount() == 3);
    assert(schema.offset(1) % alignof(double) == 0);
    expectOutOfRange([&] { schema.offset(3); });
    expectOutOfRange([&] { schema.field(3); });
    expectOutOfRange([&] { Registry::instance().get(schemaId + 1); });
    expectOutOfRange([&] { Registry::instance().get(UINT32_MAX); });
    expectOutOfRange([&] { FieldPack missing(UINT32_MAX); });

    const std::uint32_t declarationOrderId = Registry::instance().add(fields, false);
    const auto& declarationOrder = Registry::instance().get(declarationOrderId);
    assert(declarationOrder.offset(0) == sizeof(std::uint32_t));
    assert(declarationOrder.offset(1) >= declarationOrder.offset(0) + sizeof(std::int32_t));

    const std::uint32_t namedId = Registry::instance().add(fields, true, "point");
    assert(Registry::instance().get(namedId).name() == "point");
    assert(Registry::instance().find("point") == namedId);
    assert(Registry::instance().add(fields, true, "point") == namedId);
    bool rejected = false;
    try {
        Registry::instance().add(fields, false, "point", Registry::Redeclaration::Reject);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
    const std::uint32_t replacedId = Registry::instance().add(
        {fieldpack::makeTrivialField("int", sizeof(std::int32_t), alignof(std::int32_t))}, true,
        "point", Registry::Redeclaration::Replace);
    assert(replacedId != namedId);
    assert(Registry::instance().find("point") == replacedId);
    assert(Registry::instance().get(namedId).fieldCount() == 3);

    const std::uint32_t derivedId = Registry::instance().addDerived(
        namedId, {fieldpack::makeTrivialField("bool", sizeof(bool), alignof(bool))}, true, "point3d");
    const auto& derived = Registry::instance().get(derivedId);
    assert(derived.name() == "point3d");
    assert(derived.fieldCount() == 4);
    assert(derived.field(0).type == "int");
    assert(derived.field(3).type == "bool");

    // Flat FieldPack access, copy, move, and assignment.
    FieldPack pack(schemaId);
    const std::int32_t number = 42;
    const double value = 3.25;
    std::memcpy(pack.fieldAddress(0), &number, sizeof(number));
    std::memcpy(pack.fieldAddress(1), &value, sizeof(value));
    *static_cast<std::string*>(pack.fieldAddress(2)) = "hello";

    FieldPack copy(pack);
    *static_cast<std::string*>(pack.fieldAddress(2)) = "changed";
    assert(*static_cast<const std::string*>(copy.fieldAddress(2)) == "hello");

    FieldPack moved(std::move(copy));
    assert(*static_cast<const std::string*>(moved.fieldAddress(2)) == "hello");
    FieldPack assigned(schemaId);
    assigned = moved;
    assert(*static_cast<const std::string*>(assigned.fieldAddress(2)) == "hello");

    const std::uint32_t trivialSchemaId = Registry::instance().add(
        {fieldpack::makeTrivialField("int", sizeof(std::int32_t), alignof(std::int32_t)),
         fieldpack::makeTrivialField("double", sizeof(double), alignof(double))}, true);
    FieldPack trivialSource(trivialSchemaId);
    FieldPack trivialDestination(trivialSchemaId);
    const std::int32_t assignedNumber = 99;
    const double assignedValue = 7.5;
    std::memcpy(trivialSource.fieldAddress(0), &assignedNumber, sizeof(assignedNumber));
    std::memcpy(trivialSource.fieldAddress(1), &assignedValue, sizeof(assignedValue));
    trivialDestination = trivialSource;
    assert(*static_cast<const std::int32_t*>(trivialDestination.fieldAddress(0)) ==
           assignedNumber);
    assert(*static_cast<const double*>(trivialDestination.fieldAddress(1)) == assignedValue);

    FieldPack movedTrivialSource(trivialSchemaId);
    FieldPack movedTrivialTarget(std::move(movedTrivialSource));
    const std::int32_t preservedNumber = 123;
    std::memcpy(trivialDestination.fieldAddress(0), &preservedNumber, sizeof(preservedNumber));
    bool rejectedMovedFromTrivialAssignment = false;
    try {
        trivialDestination = movedTrivialSource;
    } catch (const std::logic_error&) {
        rejectedMovedFromTrivialAssignment = true;
    }
    assert(rejectedMovedFromTrivialAssignment);
    assert(*static_cast<const std::int32_t*>(trivialDestination.fieldAddress(0)) ==
           preservedNumber);

    struct alignas(64) OverAligned {
        std::uint64_t value = 0;
    };
    bool rejectedOverAligned = false;
    try {
        Registry::instance().add({fieldpack::makeField<OverAligned>("over-aligned")}, true);
    } catch (const std::invalid_argument&) {
        rejectedOverAligned = true;
    }
    assert(rejectedOverAligned);

    bool rejectedInvalidAlignment = false;
    try {
        Registry::instance().add({fieldpack::makeTrivialField("invalid", 1, 3)}, true);
    } catch (const std::invalid_argument&) {
        rejectedInvalidAlignment = true;
    }
    assert(rejectedInvalidAlignment);

    // Nested schema declarations and child storage modes.
    const std::uint32_t childSchemaId = Registry::instance().add(
        {fieldpack::makeTrivialField("int", sizeof(std::int32_t), alignof(std::int32_t)),
         fieldpack::makeField<std::string>("string")},
        true, "child");
    const std::uint32_t nestedSchemaId = Registry::instance().add(
        {fieldpack::makeSliceField(childSchemaId), fieldpack::makePackField(childSchemaId)}, true,
        "nested");
    const auto& nestedSchema = Registry::instance().get(nestedSchemaId);
    assert(nestedSchema.field(0).type == "slice");
    assert(nestedSchema.field(1).type == "pack");

    // Nested child access, deep copy, and move rebinding.
    FieldPack nested(nestedSchemaId);
    FieldPackSlice nestedSlice(nested.fieldAddress(0));
    auto* nestedPack = static_cast<FieldPack*>(nested.fieldAddress(1));
    *static_cast<std::int32_t*>(nestedSlice.fieldAddress(0)) = 11;
    *static_cast<std::string*>(nestedSlice.fieldAddress(1)) = "nested-slice";
    *static_cast<std::int32_t*>(nestedPack->fieldAddress(0)) = 22;
    *static_cast<std::string*>(nestedPack->fieldAddress(1)) = "nested-pack";

    FieldPack nestedCopy(nested);
    FieldPackSlice copiedSlice(nestedCopy.fieldAddress(0));
    auto* copiedPack = static_cast<FieldPack*>(nestedCopy.fieldAddress(1));
    assert(*static_cast<const std::int32_t*>(copiedSlice.fieldAddress(0)) == 11);
    assert(*static_cast<const std::string*>(copiedSlice.fieldAddress(1)) == "nested-slice");
    assert(*static_cast<const std::int32_t*>(copiedPack->fieldAddress(0)) == 22);
    assert(*static_cast<const std::string*>(copiedPack->fieldAddress(1)) == "nested-pack");
    *static_cast<std::int32_t*>(nestedSlice.fieldAddress(0)) = 33;
    *static_cast<std::string*>(nestedSlice.fieldAddress(1)) = "changed-slice";
    *static_cast<std::int32_t*>(nestedPack->fieldAddress(0)) = 44;
    *static_cast<std::string*>(nestedPack->fieldAddress(1)) = "changed-pack";
    assert(*static_cast<const std::int32_t*>(copiedSlice.fieldAddress(0)) == 11);
    assert(*static_cast<const std::string*>(copiedSlice.fieldAddress(1)) == "nested-slice");
    assert(*static_cast<const std::int32_t*>(copiedPack->fieldAddress(0)) == 22);
    assert(*static_cast<const std::string*>(copiedPack->fieldAddress(1)) == "nested-pack");

    FieldPack movedNested(std::move(nestedCopy));
    FieldPackSlice movedSlice(movedNested.fieldAddress(0));
    assert(*static_cast<const std::int32_t*>(movedSlice.fieldAddress(0)) == 11);
    assert(*static_cast<const std::string*>(movedSlice.fieldAddress(1)) == "nested-slice");

    // Standalone FieldPackSlice view over live storage.
    FieldPack standaloneStorage(childSchemaId);
    FieldPackSlice standaloneSlice(standaloneStorage.fieldAddress(0));
    *static_cast<std::int32_t*>(standaloneSlice.fieldAddress(0)) = 55;
    assert(*static_cast<const std::int32_t*>(standaloneSlice.fieldAddress(0)) == 55);

    // Multi-level nested slices and packs.
    const std::uint32_t outerSchemaId = Registry::instance().add(
        {fieldpack::makeSliceField(nestedSchemaId), fieldpack::makePackField(nestedSchemaId)}, true,
        "outer");
    FieldPack outer(outerSchemaId);
    FieldPackSlice outerSlice(outer.fieldAddress(0));
    auto* outerPack = static_cast<FieldPack*>(outer.fieldAddress(1));
    FieldPackSlice outerChildSlice(outerSlice.fieldAddress(0));
    *static_cast<std::int32_t*>(outerChildSlice.fieldAddress(0)) = 61;
    *static_cast<std::string*>(outerChildSlice.fieldAddress(1)) = "outer-slice-child";
    *static_cast<std::int32_t*>(static_cast<FieldPack*>(outerSlice.fieldAddress(1))
                                     ->fieldAddress(0)) = 62;
    FieldPackSlice outerPackSlice(outerPack->fieldAddress(0));
    *static_cast<std::int32_t*>(outerPackSlice.fieldAddress(0)) = 63;
    *static_cast<std::int32_t*>(static_cast<FieldPack*>(outerPack->fieldAddress(1))
                                     ->fieldAddress(0)) = 64;
    assert(*static_cast<const std::int32_t*>(outerChildSlice.fieldAddress(0)) == 61);
    assert(*static_cast<const std::int32_t*>(static_cast<FieldPack*>(outerSlice.fieldAddress(1))
                                                  ->fieldAddress(0)) == 62);
    assert(*static_cast<const std::int32_t*>(outerPackSlice.fieldAddress(0)) == 63);
    assert(*static_cast<const std::int32_t*>(static_cast<FieldPack*>(outerPack->fieldAddress(1))
                                                  ->fieldAddress(0)) == 64);

    FieldPack outerCopy(outer);
    FieldPackSlice copiedOuterSlice(outerCopy.fieldAddress(0));
        FieldPackSlice copiedOuterChild(copiedOuterSlice.fieldAddress(0));
        assert(*static_cast<const std::int32_t*>(copiedOuterChild.fieldAddress(0)) == 61);
        assert(*static_cast<const std::string*>(copiedOuterChild.fieldAddress(1)) ==
            "outer-slice-child");
    FieldPack outerAssigned(childSchemaId);
    outerAssigned = std::move(outerCopy);
    FieldPackSlice assignedOuterSlice(outerAssigned.fieldAddress(0));
    FieldPackSlice assignedOuterChild(assignedOuterSlice.fieldAddress(0));
    assert(*static_cast<const std::int32_t*>(assignedOuterChild.fieldAddress(0)) == 61);

    FieldPack movedFrom(childSchemaId);
    FieldPack movedTo(std::move(movedFrom));
    bool rejectedMovedFromCopy = false;
    try {
        FieldPack invalidCopy(movedFrom);
    } catch (const std::logic_error&) {
        rejectedMovedFromCopy = true;
    }
    assert(rejectedMovedFromCopy);
    FieldPack copyAssignmentTarget(childSchemaId);
    rejectedMovedFromCopy = false;
    try {
        copyAssignmentTarget = movedFrom;
    } catch (const std::logic_error&) {
        rejectedMovedFromCopy = true;
    }
    assert(rejectedMovedFromCopy);

    const std::uint32_t alternateNestedId = Registry::instance().add(
        {fieldpack::makePackField(childSchemaId)}, true, "nested", Registry::Redeclaration::Replace);
    assert(alternateNestedId != nestedSchemaId);

    expectOutOfRange([&] { pack.fieldAddress(3); });
    return 0;
}