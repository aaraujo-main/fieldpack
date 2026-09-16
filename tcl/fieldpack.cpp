#include "fieldpack/fieldpack.hpp"
#include "tclxx.hpp"

#include <tcl.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using fieldpack::FieldPack;
using fieldpack::FieldOps;
using fieldpack::Registry;

void constructObj(void* address, const FieldOps&) { *static_cast<Tcl_Obj**>(address) = nullptr; }
void copyObj(const void* source, void* destination, std::size_t, const FieldOps&) {
    Tcl_Obj* value = *static_cast<Tcl_Obj* const*>(source);
    *static_cast<Tcl_Obj**>(destination) = value;
    if (value) Tcl_IncrRefCount(value);
}
void copyConstructObj(const void* source, void* destination, std::size_t, const FieldOps&) {
    Tcl_Obj* value = *static_cast<Tcl_Obj* const*>(source);
    new (destination) Tcl_Obj*(value);
    if (value) Tcl_IncrRefCount(value);
}
void destroyObj(void* address, const FieldOps&) noexcept {
    Tcl_Obj** value = static_cast<Tcl_Obj**>(address);
    if (*value) Tcl_DecrRefCount(*value);
    *value = nullptr;
}

FieldOps objField() {
        return {"obj", sizeof(Tcl_Obj*), alignof(Tcl_Obj*), false, &constructObj, &copyObj,
            &copyConstructObj, &destroyObj, nullptr};
}

std::string typeName(Tcl_Obj* object) {
    return Tcl_GetString(object);
}

std::uint32_t nestedSchemaId(Tcl_Interp* interp, Tcl_Obj* object) {
    Tcl_WideInt raw = 0;
    if (Tcl_GetWideIntFromObj(interp, object, &raw) == TCL_OK && raw >= 0) {
        return static_cast<std::uint32_t>(raw);
    }
    Tcl_ResetResult(interp);
    const auto id = Registry::instance().find(Tcl_GetString(object));
    if (!id) throw std::out_of_range("nested schema not found");
    return *id;
}

FieldOps parseType(Tcl_Interp* interp, Tcl_Obj* object) {
    int count = 0;
    Tcl_Obj** elements = nullptr;
    if (Tcl_ListObjGetElements(interp, object, &count, &elements) == TCL_OK && count == 2) {
        const std::string kind = typeName(elements[0]);
        const std::uint32_t schemaId = nestedSchemaId(interp, elements[1]);
        if (kind == "slice") return fieldpack::makeSliceField(schemaId);
        if (kind == "pack") return fieldpack::makePackField(schemaId);
    }
    Tcl_ResetResult(interp);
    const std::string type = typeName(object);
    if (type == "int") return fieldpack::makeTrivialField(type, sizeof(std::int32_t), alignof(std::int32_t));
    if (type == "double") return fieldpack::makeTrivialField(type, sizeof(double), alignof(double));
    if (type == "bool") return fieldpack::makeTrivialField(type, sizeof(bool), alignof(bool));
    if (type == "string") return fieldpack::makeField<std::string>(type);
    if (type == "obj") return objField();
    throw std::invalid_argument("unsupported field type: " + type);
}

using FieldAddress = std::function<void*(std::size_t)>;

Tcl_Obj* serializeSchema(const fieldpack::Schema& schema, const void* storage);
Tcl_Obj* serializePack(const FieldPack& pack);
void parseSchemaValue(Tcl_Interp* interp, const fieldpack::Schema& schema, Tcl_Obj* value,
                      const FieldAddress& address);

FieldPack nestedValue(const FieldOps& ops, const void* address) {
    if (!ops.nested) throw std::invalid_argument("field is not nested");
    if (ops.nested->kind == fieldpack::NestedKind::Slice) {
        const fieldpack::FieldPackSlice slice(const_cast<void*>(address));
        const auto& schema = slice.schema();
        FieldPack result(schema.id());
        for (std::size_t index = 0; index < schema.fieldCount(); ++index) {
            const FieldOps& field = schema.field(index);
            void* target = result.fieldAddress(index);
            const void* source = slice.fieldAddress(index);
            if (field.rawCopySafe) {
                std::memcpy(target, source, field.size);
            } else {
                field.copy(source, target, field.size, field);
            }
        }
        return result;
    }
    return *static_cast<const FieldPack*>(address);
}

Tcl_Obj* getValue(const FieldOps& ops, const void* address) {
    if (ops.type == "int") return Tcl_NewIntObj(*static_cast<const std::int32_t*>(address));
    if (ops.type == "double") return Tcl_NewDoubleObj(*static_cast<const double*>(address));
    if (ops.type == "bool") return Tcl_NewBooleanObj(*static_cast<const bool*>(address));
    if (ops.type == "string") {
        const auto& value = *static_cast<const std::string*>(address);
        return Tcl_NewStringObj(value.c_str(), static_cast<int>(value.size()));
    }
    if (ops.type == "obj") {
        Tcl_Obj* value = *static_cast<Tcl_Obj* const*>(address);
        return value ? value : Tcl_NewObj();
    }
    if (ops.nested) return tclxx::ObjType<FieldPack>::New(new FieldPack(nestedValue(ops, address)));
    throw std::invalid_argument("unsupported field type");
}

void appendSerializedField(Tcl_Obj* list, const FieldOps& ops, const void* address) {
    if (!ops.nested) {
        Tcl_ListObjAppendElement(nullptr, list, getValue(ops, address));
        return;
    }

    Tcl_Obj* nested = nullptr;
    if (ops.nested->kind == fieldpack::NestedKind::Slice) {
        const fieldpack::FieldPackSlice slice(const_cast<void*>(address));
        nested = serializeSchema(slice.schema(), address);
    } else {
        nested = serializePack(*static_cast<const FieldPack*>(address));
    }
    Tcl_ListObjAppendElement(nullptr, list, nested);
}

Tcl_Obj* serializeSchema(const fieldpack::Schema& schema, const void* storage) {
    Tcl_Obj* list = Tcl_NewListObj(0, nullptr);
    Tcl_ListObjAppendElement(nullptr, list, Tcl_NewStringObj(schema.name().c_str(), -1));
    const auto* bytes = static_cast<const std::uint8_t*>(storage);
    for (std::size_t index = 0; index < schema.fieldCount(); ++index) {
        const FieldOps& ops = schema.field(index);
        appendSerializedField(list, ops, bytes + schema.offset(index));
    }
    return list;
}

Tcl_Obj* serializePack(const FieldPack& pack) {
    Tcl_Obj* list = Tcl_NewListObj(0, nullptr);
    Tcl_ListObjAppendElement(nullptr, list, Tcl_NewStringObj(pack.schema().name().c_str(), -1));
    for (std::size_t index = 0; index < pack.schema().fieldCount(); ++index) {
        const FieldOps& ops = pack.schema().field(index);
        appendSerializedField(list, ops, pack.fieldAddress(index));
    }
    return list;
}

void setValue(Tcl_Interp* interp, const FieldOps& ops, void* address, Tcl_Obj* value) {
    if (ops.type == "int") {
        *static_cast<std::int32_t*>(address) = tclxx::obj_cast::to<std::int32_t>(interp, value);
    } else if (ops.type == "double") {
        *static_cast<double*>(address) = tclxx::obj_cast::to<double>(interp, value);
    } else if (ops.type == "bool") {
        *static_cast<bool*>(address) = tclxx::obj_cast::to<bool>(interp, value);
    } else if (ops.type == "string") {
        *static_cast<std::string*>(address) = Tcl_GetString(value);
    } else if (ops.type == "obj") {
        auto** stored = static_cast<Tcl_Obj**>(address);
        Tcl_IncrRefCount(value);
        if (*stored) Tcl_DecrRefCount(*stored);
        *stored = value;
    } else if (ops.nested) {
        FieldPack parsed = tclxx::obj_cast::to<FieldPack>(interp, value);
        if (parsed.schemaId() != ops.nested->schemaId) {
            throw std::invalid_argument("FieldPack value has wrong schema");
        }
        const auto& schema = Registry::instance().get(ops.nested->schemaId);
        if (ops.nested->kind == fieldpack::NestedKind::Slice) {
        auto* destination = static_cast<std::uint8_t*>(address);
        for (std::size_t index = 0; index < schema.fieldCount(); ++index) {
            const FieldOps& field = schema.field(index);
            void* target = destination + schema.offset(index);
            if (field.rawCopySafe) {
                std::memcpy(target, parsed.fieldAddress(index), field.size);
            } else {
                field.copy(parsed.fieldAddress(index), target, field.size, field);
            }
        }
        } else if (ops.nested->kind == fieldpack::NestedKind::Pack) {
            *static_cast<FieldPack*>(address) = std::move(parsed);
        } else {
            throw std::invalid_argument("unsupported field type");
        }
    } else {
        throw std::invalid_argument("unsupported field type");
    }
}

void parseNestedValue(Tcl_Interp* interp, const FieldOps& ops, void* address, Tcl_Obj* value) {
    const auto nestedSchemaId = ops.nested->schemaId;
    const auto& nestedSchema = Registry::instance().get(nestedSchemaId);
    if (ops.nested->kind == fieldpack::NestedKind::Slice) {
        parseSchemaValue(interp, nestedSchema, value, [address, &nestedSchema](std::size_t index) {
            return static_cast<std::uint8_t*>(address) + nestedSchema.offset(index);
        });
    } else {
        auto* pack = static_cast<FieldPack*>(address);
        parseSchemaValue(interp, nestedSchema, value, [pack](std::size_t index) {
            return pack->fieldAddress(index);
        });
    }
}

void parseSchemaValue(Tcl_Interp* interp, const fieldpack::Schema& schema, Tcl_Obj* value,
                     const FieldAddress& address) {
    int count = 0;
    Tcl_Obj** elements = nullptr;
    if (Tcl_ListObjGetElements(interp, value, &count, &elements) != TCL_OK) {
        throw std::invalid_argument("FieldPack value must be a list");
    }
    if (count != static_cast<int>(schema.fieldCount() + 1)) {
        throw std::invalid_argument("FieldPack value has wrong field count");
    }
    if (Tcl_GetString(elements[0]) != schema.name()) {
        throw std::invalid_argument("FieldPack value has wrong schema name");
    }
    for (std::size_t index = 0; index < schema.fieldCount(); ++index) {
        const FieldOps& ops = schema.field(index);
        void* field = address(index);
        if (ops.nested) {
            parseNestedValue(interp, ops, field, elements[index + 1]);
        } else {
            setValue(interp, ops, field, elements[index + 1]);
        }
    }
}

FieldPack parsePackValue(Tcl_Interp* interp, Tcl_Obj* value) {
    int count = 0;
    Tcl_Obj** elements = nullptr;
    if (Tcl_ListObjGetElements(interp, value, &count, &elements) != TCL_OK || count == 0) {
        throw std::invalid_argument("FieldPack value must start with schema name");
    }
    const auto schemaId = Registry::instance().find(Tcl_GetString(elements[0]));
    if (!schemaId) throw std::out_of_range("schema name not found");
    FieldPack pack(*schemaId);
    parseSchemaValue(interp, pack.schema(), value, [&pack](std::size_t index) {
        return pack.fieldAddress(index);
    });
    return pack;
}

struct ResolvedField {
    const FieldOps* ops;
    void* address;
};

ResolvedField resolvePath(Tcl_Interp* interp, FieldPack* pack, Tcl_Obj* path) {
    int count = 0;
    Tcl_Obj** elements = nullptr;
    if (Tcl_ListObjGetElements(interp, path, &count, &elements) != TCL_OK || count == 0) {
        throw std::invalid_argument("field path must contain at least one index");
    }
    std::uint32_t schemaId = pack->schemaId();
    std::function<void*(std::size_t)> address = [pack](std::size_t index) {
        return pack->fieldAddress(index);
    };
    for (int position = 0; position < count; ++position) {
        Tcl_WideInt rawIndex = 0;
        if (Tcl_GetWideIntFromObj(interp, elements[position], &rawIndex) != TCL_OK || rawIndex < 0) {
            throw std::invalid_argument("field path index must be non-negative");
        }
        const std::size_t index = static_cast<std::size_t>(rawIndex);
        const auto& schema = Registry::instance().get(schemaId);
        const FieldOps& ops = schema.field(index);
        void* field = address(index);
        if (position == count - 1) return {&ops, field};
        if (!ops.nested) throw std::invalid_argument("field path enters non-nested field");
        if (ops.nested->kind == fieldpack::NestedKind::Slice) {
            schemaId = ops.nested->schemaId;
            address = [field, schemaId](std::size_t childIndex) {
                return static_cast<std::uint8_t*>(field) +
                       Registry::instance().get(schemaId).offset(childIndex);
            };
        } else {
            auto* child = static_cast<FieldPack*>(field);
            schemaId = child->schemaId();
            address = [child](std::size_t childIndex) { return child->fieldAddress(childIndex); };
        }
    }
    throw std::logic_error("empty field path");
}

Tcl_Obj* getField(FieldPack* pack, int index) {
    if (index < 0) throw std::out_of_range("field index out of range");
    const std::size_t fieldIndex = static_cast<std::size_t>(index);
    return getValue(pack->schema().field(fieldIndex), pack->fieldAddress(fieldIndex));
}

std::string getSchemaName(FieldPack* pack) { return pack->schema().name(); }

int getSchemaId(FieldPack* pack) { return static_cast<int>(pack->schemaId()); }

void setField(FieldPack* pack, int index, Tcl_Obj* value) {
    if (index < 0) throw std::out_of_range("field index out of range");
    const std::size_t fieldIndex = static_cast<std::size_t>(index);
    setValue(nullptr, pack->schema().field(fieldIndex), pack->fieldAddress(fieldIndex), value);
}

int getPathCommand(ClientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    try {
        if (objc != 3) throw std::invalid_argument("usage: fieldpack::get_path pack path");
        const ResolvedField field = resolvePath(interp,
                                                tclxx::obj_cast::to<FieldPack*>(interp, objv[1]),
                                                objv[2]);
        Tcl_SetObjResult(interp, getValue(*field.ops, field.address));
        return TCL_OK;
    } catch (const std::exception& error) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(error.what(), -1));
        return TCL_ERROR;
    }
}

int setPathCommand(ClientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    Tcl_Obj* packObject = nullptr;
    try {
        if (objc != 4) throw std::invalid_argument("usage: fieldpack::set_path packVarName path value");
        packObject = Tcl_ObjGetVar2(interp, objv[1], nullptr, TCL_LEAVE_ERR_MSG);
        if (!packObject) return TCL_ERROR;
        if (Tcl_IsShared(packObject)) {
            packObject = Tcl_DuplicateObj(packObject);
            if (!Tcl_ObjSetVar2(interp, objv[1], nullptr, packObject, TCL_LEAVE_ERR_MSG)) return TCL_ERROR;
        }
        const ResolvedField field = resolvePath(interp,
                                                tclxx::obj_cast::to<FieldPack*>(interp, packObject),
                                                objv[2]);
        setValue(interp, *field.ops, field.address, objv[3]);
        Tcl_InvalidateStringRep(packObject);
        return TCL_OK;
    } catch (const std::exception& error) {
        if (packObject) Tcl_InvalidateStringRep(packObject);
        Tcl_SetObjResult(interp, Tcl_NewStringObj(error.what(), -1));
        return TCL_ERROR;
    }
}

int updateCommand(ClientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    Tcl_Obj* packObject = nullptr;
    bool fieldVarBound = false;
    Tcl_SavedResult savedResult;
    bool resultSaved = false;
    int bodyCode = TCL_OK;
    std::string writeBackError;

    auto clearFieldVar = [&]() {
        Tcl_Obj* empty = Tcl_NewStringObj("", 0);
        Tcl_ObjSetVar2(interp, objv[3], nullptr, empty, 0);
    };

    try {
        if (objc != 5) {
            throw std::invalid_argument(
                "usage: fieldpack::update packVarName fieldIndex fieldVarName body");
        }

        Tcl_WideInt rawIndex = 0;
        if (Tcl_GetWideIntFromObj(interp, objv[2], &rawIndex) != TCL_OK) return TCL_ERROR;
        if (rawIndex < 0) throw std::out_of_range("field index out of range");

        packObject = Tcl_ObjGetVar2(interp, objv[1], nullptr, TCL_LEAVE_ERR_MSG);
        if (!packObject) return TCL_ERROR;
        if (Tcl_IsShared(packObject)) {
            packObject = Tcl_DuplicateObj(packObject);
            if (!Tcl_ObjSetVar2(interp, objv[1], nullptr, packObject, TCL_LEAVE_ERR_MSG)) {
                return TCL_ERROR;
            }
        }

        auto* pack = tclxx::obj_cast::to<FieldPack*>(interp, packObject);
        if (!pack) throw std::runtime_error("Cannot invoke fieldpack::update on null object handle");
        const std::size_t fieldIndex = static_cast<std::size_t>(rawIndex);
        const FieldOps& ops = pack->schema().field(fieldIndex);

        if (!Tcl_ObjSetVar2(interp, objv[3], nullptr,
                            getValue(ops, pack->fieldAddress(fieldIndex)), TCL_LEAVE_ERR_MSG)) {
            return TCL_ERROR;
        }
        fieldVarBound = true;

        bodyCode = Tcl_EvalObjEx(interp, objv[4], 0);
        Tcl_SaveResult(interp, &savedResult);
        resultSaved = true;

        Tcl_Obj* updated = Tcl_ObjGetVar2(interp, objv[3], nullptr, 0);
        if (!updated) {
            if (bodyCode == TCL_OK) {
                writeBackError = "fieldVar must remain defined during fieldpack::update body";
            }
        } else {
            try {
                setValue(interp, ops, pack->fieldAddress(fieldIndex), updated);
            } catch (const std::exception& error) {
                writeBackError = error.what();
            }
        }

        clearFieldVar();
        Tcl_InvalidateStringRep(packObject);
        Tcl_RestoreResult(interp, &savedResult);
        resultSaved = false;
        if (!writeBackError.empty()) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(writeBackError.c_str(), -1));
            return TCL_ERROR;
        }
        return bodyCode;
    } catch (const std::exception& error) {
        if (resultSaved) Tcl_DiscardResult(&savedResult);
        if (packObject) Tcl_InvalidateStringRep(packObject);
        if (fieldVarBound) clearFieldVar();
        Tcl_SetObjResult(interp, Tcl_NewStringObj(error.what(), -1));
        return TCL_ERROR;
    } catch (...) {
        if (resultSaved) Tcl_DiscardResult(&savedResult);
        if (packObject) Tcl_InvalidateStringRep(packObject);
        if (fieldVarBound) clearFieldVar();
        Tcl_SetObjResult(interp, Tcl_NewStringObj("unknown C++ exception", -1));
        return TCL_ERROR;
    }
}

int schemaRegisterCommand(ClientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    try {
        if (objc < 3 || objc > 5) {
            throw std::invalid_argument(
                "usage: fieldpack::schema::register name types ?optimize? ?redeclare?");
        }
        int count = 0;
        Tcl_Obj** elements = nullptr;
        if (Tcl_ListObjGetElements(interp, objv[2], &count, &elements) != TCL_OK) return TCL_ERROR;
        std::vector<FieldOps> fields;
        fields.reserve(static_cast<std::size_t>(count));
        for (int index = 0; index < count; ++index) fields.push_back(parseType(interp, elements[index]));
        int optimize = 0;
        if (objc >= 4 && Tcl_GetBooleanFromObj(interp, objv[3], &optimize) != TCL_OK) return TCL_ERROR;
        Registry::Redeclaration policy = Registry::Redeclaration::ReuseEquivalent;
        if (objc == 5) {
            const std::string mode = Tcl_GetString(objv[4]);
            if (mode == "reject") {
                policy = Registry::Redeclaration::Reject;
            } else if (mode == "replace") {
                policy = Registry::Redeclaration::Replace;
            } else if (mode != "reuse-equivalent") {
                throw std::invalid_argument("redeclare must be reuse-equivalent, reject, or replace");
            }
        }
        const std::string name = Tcl_GetString(objv[1]);
        const auto id = name.empty()
                            ? Registry::instance().registerAnonymous(std::move(fields), optimize)
                            : Registry::instance().registerNamed(std::move(name), std::move(fields),
                                                                 optimize, policy);
        return Tcl_SetObjResult(interp, Tcl_NewWideIntObj(id)), TCL_OK;
    } catch (const std::exception& error) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(error.what(), -1));
        return TCL_ERROR;
    }
}

std::uint32_t schemaId(Tcl_Interp* interp, Tcl_Obj* object) {
    Tcl_WideInt raw = 0;
    if (Tcl_GetWideIntFromObj(interp, object, &raw) == TCL_OK && raw >= 0) {
        return static_cast<std::uint32_t>(raw);
    }
    Tcl_ResetResult(interp);
    const auto id = Registry::instance().find(Tcl_GetString(object));
    if (!id) throw std::out_of_range("base schema not found");
    return *id;
}

int schemaDeriveCommand(ClientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    try {
        if (objc < 4 || objc > 6) {
            throw std::invalid_argument(
                "usage: fieldpack::schema::derive name base types ?optimize? ?redeclare?");
        }
        const std::string name = Tcl_GetString(objv[1]);
        const std::uint32_t baseId = schemaId(interp, objv[2]);
        const auto& base = Registry::instance().get(baseId);
        int count = 0;
        Tcl_Obj** elements = nullptr;
        if (Tcl_ListObjGetElements(interp, objv[3], &count, &elements) != TCL_OK) return TCL_ERROR;
        std::vector<FieldOps> fields;
        fields.reserve(static_cast<std::size_t>(count));
        for (int index = 0; index < count; ++index) fields.push_back(parseType(interp, elements[index]));
        int optimize = base.optimize();
        if (objc >= 5 && Tcl_GetBooleanFromObj(interp, objv[4], &optimize) != TCL_OK) return TCL_ERROR;
        Registry::Redeclaration policy = Registry::Redeclaration::ReuseEquivalent;
        if (objc == 6) {
            const std::string mode = Tcl_GetString(objv[5]);
            if (mode == "reject") {
                policy = Registry::Redeclaration::Reject;
            } else if (mode == "replace") {
                policy = Registry::Redeclaration::Replace;
            } else if (mode != "reuse-equivalent") {
                throw std::invalid_argument("redeclare must be reuse-equivalent, reject, or replace");
            }
        }
        const auto id = name.empty()
                            ? Registry::instance().deriveAnonymous(baseId, std::move(fields), optimize)
                            : Registry::instance().deriveNamed(std::move(name), baseId,
                                                               std::move(fields), optimize, policy);
        return Tcl_SetObjResult(interp, Tcl_NewWideIntObj(id)), TCL_OK;
    } catch (const std::exception& error) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(error.what(), -1));
        return TCL_ERROR;
    }
}

int schemaExistsCommand(ClientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    try {
        if (objc != 2) throw std::invalid_argument("usage: fieldpack::schema::exists name");
        return Tcl_SetObjResult(interp, Tcl_NewBooleanObj(
            Registry::instance().find(Tcl_GetString(objv[1])).has_value())), TCL_OK;
    } catch (const std::exception& error) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(error.what(), -1));
        return TCL_ERROR;
    }
}

int schemaIdCommand(ClientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    try {
        if (objc != 2) throw std::invalid_argument("usage: fieldpack::schema::id name");
        const auto id = Registry::instance().find(Tcl_GetString(objv[1]));
        if (!id) throw std::out_of_range("schema name not found");
        return Tcl_SetObjResult(interp, Tcl_NewWideIntObj(*id)), TCL_OK;
    } catch (const std::exception& error) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(error.what(), -1));
        return TCL_ERROR;
    }
}

int schemaFieldCountCommand(ClientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    try {
        if (objc != 2) throw std::invalid_argument("usage: fieldpack::schema::field_count schemaId");
        Tcl_WideInt raw = 0;
        if (Tcl_GetWideIntFromObj(interp, objv[1], &raw) != TCL_OK || raw < 0) return TCL_ERROR;
        const auto& schema = Registry::instance().get(static_cast<std::uint32_t>(raw));
        return Tcl_SetObjResult(interp, Tcl_NewWideIntObj(static_cast<Tcl_WideInt>(schema.fieldCount()))), TCL_OK;
    } catch (const std::exception& error) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(error.what(), -1));
        return TCL_ERROR;
    }
}

int schemaFieldTypeCommand(ClientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    try {
        if (objc != 3) throw std::invalid_argument("usage: fieldpack::schema::field_type schemaId index");
        Tcl_WideInt raw = 0;
        Tcl_WideInt index = 0;
        if (Tcl_GetWideIntFromObj(interp, objv[1], &raw) != TCL_OK || raw < 0) return TCL_ERROR;
        if (Tcl_GetWideIntFromObj(interp, objv[2], &index) != TCL_OK || index < 0) return TCL_ERROR;
        const auto& schema = Registry::instance().get(static_cast<std::uint32_t>(raw));
        return Tcl_SetObjResult(interp, Tcl_NewStringObj(schema.field(static_cast<std::size_t>(index)).type.c_str(), -1)), TCL_OK;
    } catch (const std::exception& error) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(error.what(), -1));
        return TCL_ERROR;
    }
}

int schemaSizeCommand(ClientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    try {
        if (objc != 2) throw std::invalid_argument("usage: fieldpack::schema::size schemaId");
        Tcl_WideInt raw = 0;
        if (Tcl_GetWideIntFromObj(interp, objv[1], &raw) != TCL_OK || raw < 0) return TCL_ERROR;
        const auto& schema = Registry::instance().get(static_cast<std::uint32_t>(raw));
        return Tcl_SetObjResult(interp, Tcl_NewWideIntObj(static_cast<Tcl_WideInt>(schema.totalSize()))), TCL_OK;
    } catch (const std::exception& error) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(error.what(), -1));
        return TCL_ERROR;
    }
}

int schemaNameCommand(ClientData, Tcl_Interp* interp, int objc, Tcl_Obj* const objv[]) {
    try {
        if (objc != 2) throw std::invalid_argument("usage: fieldpack::schema::name schemaId");
        Tcl_WideInt raw = 0;
        if (Tcl_GetWideIntFromObj(interp, objv[1], &raw) != TCL_OK || raw < 0) return TCL_ERROR;
        const auto& schema = Registry::instance().get(static_cast<std::uint32_t>(raw));
        return Tcl_SetObjResult(interp, Tcl_NewStringObj(schema.name().c_str(), -1)), TCL_OK;
    } catch (const std::exception& error) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj(error.what(), -1));
        return TCL_ERROR;
    }
}

} // namespace

namespace tclxx {
template<>
std::string ObjType<fieldpack::FieldPack>::ToString(const fieldpack::FieldPack& value) {
    Tcl_Obj* list = serializePack(value);
    const std::string result = Tcl_GetString(list);
    Tcl_IncrRefCount(list);
    Tcl_DecrRefCount(list);
    return result;
}

template<>
fieldpack::FieldPack ObjType<fieldpack::FieldPack>::FromAny(Tcl_Interp* interp, Tcl_Obj* const value) {
    return parsePackValue(interp, value);
}
} // namespace tclxx

extern "C" int Fieldpack_Init(Tcl_Interp* interp) {
    if (!interp) return TCL_ERROR;
    if (Tcl_InitStubs(interp, TCL_VERSION, 0) == nullptr) return TCL_ERROR;
    Tcl_CreateNamespace(interp, "::fieldpack", nullptr, nullptr);
    Tcl_CreateNamespace(interp, "::fieldpack::schema", nullptr, nullptr);
    Tcl_CreateObjCommand(interp, "::fieldpack::schema::register", schemaRegisterCommand, nullptr,
                         nullptr);
    Tcl_CreateObjCommand(interp, "::fieldpack::schema::derive", schemaDeriveCommand, nullptr, nullptr);
    Tcl_CreateObjCommand(interp, "::fieldpack::schema::exists", schemaExistsCommand, nullptr, nullptr);
    Tcl_CreateObjCommand(interp, "::fieldpack::schema::id", schemaIdCommand, nullptr, nullptr);
    Tcl_CreateObjCommand(interp, "::fieldpack::schema::field_count", schemaFieldCountCommand, nullptr, nullptr);
    Tcl_CreateObjCommand(interp, "::fieldpack::schema::field_type", schemaFieldTypeCommand, nullptr, nullptr);
    Tcl_CreateObjCommand(interp, "::fieldpack::schema::size", schemaSizeCommand, nullptr, nullptr);
    Tcl_CreateObjCommand(interp, "::fieldpack::schema::name", schemaNameCommand, nullptr, nullptr);
    TCLXX_CMD_NEW(interp, "::fieldpack::new", fieldpack::FieldPack, int);
    TCLXX_CMD_GETTER(interp, "::fieldpack::get", &getField);
    TCLXX_CMD_GETTER(interp, "::fieldpack::get_schema_name", &getSchemaName);
    TCLXX_CMD_GETTER(interp, "::fieldpack::get_schema_id", &getSchemaId);
    TCLXX_CMD_SETTER(interp, "::fieldpack::set", &setField);
    Tcl_CreateObjCommand(interp, "::fieldpack::get_path", getPathCommand, nullptr, nullptr);
    Tcl_CreateObjCommand(interp, "::fieldpack::set_path", setPathCommand, nullptr, nullptr);
    Tcl_CreateObjCommand(interp, "::fieldpack::update", updateCommand, nullptr, nullptr);
    return Tcl_PkgProvide(interp, "fieldpack", "1.0.0");
}
extern "C" int FieldPack_Init(Tcl_Interp* interp) { return Fieldpack_Init(interp); }
