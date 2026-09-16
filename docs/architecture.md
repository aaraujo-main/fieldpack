# FieldPack Architecture

## File boundaries

FieldPack core is split by ownership and behavior:

- `field_ops.hpp` and `field_ops.cpp` define type-erased field callbacks and trivial-field handling.
- `schema.hpp` and `schema.cpp` calculate storage layout and map logical fields to offsets.
- `registry.hpp` and `registry.cpp` own process-local schema IDs and schema lifetime.
- `fieldpack.hpp` and `fieldpack.cpp` manage byte-buffer instances, inline slices, and field lifecycles.

`fieldpack/fieldpack.hpp` remains an umbrella header. Existing consumers keep their include path and public names. Focused headers are available when a consumer needs narrower dependencies.

Dependency direction is `FieldPack -> Registry -> Schema -> FieldOps`. Each implementation file owns one runtime concern, reducing rebuild scope and making lifecycle behavior easier to test.

## Header-defined templates

`construct<T>`, `copy<T>`, `destroy<T>`, and `makeField<T>` remain in `field_ops.hpp`. Template definitions must be visible where callers instantiate them. Non-template trivial callbacks stay in `field_ops.cpp`.

## Layout and lifecycle invariants

Schema construction preserves logical field indexes while optionally sorting physical placement by alignment. Logical indexes are user-visible field positions; physical offsets are byte locations inside the buffer. FieldPack construction invokes callbacks in logical order; rollback and destruction invoke callbacks in reverse order. FieldPack owns independent storage, while each field callback defines copy semantics. Move transfers storage ownership.

Registry names are optional aliases for process-local schema IDs. Equivalent named registrations reuse the existing ID. A divergent named registration rejects by default; explicit replacement allocates a new ID and remaps the name. Old IDs and their schemas remain valid for process lifetime, preserving live `FieldPack` objects. Empty names behave like unnamed registration.

Each `Schema` stores its registration name and exposes it through `name()`. `Registry::addDerived` copies a base schema's `FieldOps` in logical order, appends supplied fields, and registers the resulting schema as an independent layout. `Registry::registerAnonymous` and `Registry::registerNamed` make anonymous and named registration explicit. Tcl exposes this through `schema::register name types` and `schema::derive name base types`; empty name selects anonymous registration, and `schema::name` reads stored name.

## Copy optimization

`FieldOps::rawCopySafe` identifies fields whose bytes can be copied without object-lifetime or ownership callbacks. A non-trivial field requires lifecycle or copy callbacks. `makeTrivialField` sets this flag; `makeField<T>` stays conservative because arbitrary types may require constructors, assignment, copying, or destruction even when their bytes look simple. Tcl object fields remain non-trivial because each stored `Tcl_Obj*` owns one Tcl reference: copying increments the reference count and destruction decrements it.

FieldPack copy construction first copies schema ID and bytes for each raw-copy-safe field. It then placement-copy-constructs each cached non-trivial field. Non-trivial field bytes are never copied as raw storage. If a copy constructor throws, only successfully placement-constructed fields are destroyed. Assignment callbacks remain separate from copy-construction callbacks and are not used on raw destination storage.

Schema caches non-trivial logical indexes. Default construction and destruction skip raw-safe fields, while preserving construction order, reverse destruction order, and rollback behavior.

Same-schema assignment uses one raw copy when every field is raw-copy-safe, avoiding temporary allocation. Mixed and non-trivial schemas retain copy-and-move assignment so a throwing field assignment keeps the existing strong exception guarantee.

## Nested storage policies

Nested fields use one schema with two storage policies:

- `slice` embeds child storage inside the parent buffer. Child bytes remain physically continuous with parent bytes, so construction needs no child allocation. Parent storage owner remains responsible for the byte region and child field lifetimes. `FieldPackSlice` is only a non-owning access view.
- `pack` stores a `FieldPack` object in the parent field, but that object owns a separate allocation for its child storage. Child size and lifetime are independent from the parent buffer.

Both policies use the same schema lookup, field offsets, and lifecycle callbacks. `FieldOps::nested` stores immutable nested metadata: child schema ID and `NestedKind`. Schema equivalence compares this metadata, so `slice Point`, `pack Point`, and references to different schema IDs remain distinct definitions.

`FieldPackSlice` is a non-allocating, non-owning view over caller-provided storage. Parent field callbacks construct, copy, and destroy child fields directly in the inline region. `FieldPack` owns its own allocation. This keeps storage ownership and field lifetime ownership in one place: parent owns both inline bytes and inline child lifetimes.

Nested slice fields contain no view object or pointer. Moving an owning `FieldPack` therefore needs no slice rebinding. Copy construction copies child fields into destination storage. Owned `pack` children do not need rebinding because their allocations move independently.

Nested schemas are composed from already registered schema IDs. Tcl descriptors resolve names or numeric IDs during schema registration, then store IDs rather than mutable names. This keeps existing layouts valid when a name is later replaced. Layout size includes inline slice storage and only the owning child object for `pack` fields; the owned child allocation is created by its field constructor.

## Tcl nested API

Tcl declares nested fields with list descriptors:

```tcl
{slice SchemaName}
{pack SchemaName}
```

`slice` and `pack` are explicit instead of inferring ownership from a bare schema name. This avoids ambiguity and leaves room for future modifiers. `get_path` accepts a FieldPack value; `set_path` accepts a FieldPack variable name so it can preserve copy-on-write behavior. Both accept logical index lists, resolve each nested container, and operate on the final scalar field. They do not expose borrowed slice handles, preventing a child handle from outliving its parent storage.

Nested fields reject direct scalar `get` and `set`; callers must provide a path. Path traversal supports arbitrary nesting and treats indexes as logical schema indexes, independent of alignment-optimized physical offsets.

## Tcl FieldPack representation

Tcl string conversion uses a list representation for complete `FieldPack` values:

```tcl
SchemaName scalarValue ... {ChildSchema nestedValue ...}
```

The first list element is the registered schema name. Remaining elements follow logical
field order. Scalar values use Tcl's native list elements, so strings containing spaces
are emitted with Tcl quoting such as `{hello world}`. Nested `slice` and `pack` fields
emit the same representation recursively; their storage policy comes from the schema's
`FieldOps::nested` metadata rather than from serialized syntax.

`FieldPack` conversion from Tcl reverses this process. `FromAny` resolves the first list
element through the process-local registry, constructs the resolved schema, then parses
each field according to its registered type. It validates schema name, field count, and
nested schema shape. Unknown schemas, malformed lists, invalid scalar values, and nested
shape mismatches return Tcl conversion errors. This representation is intended for Tcl
interchange and diagnostics; schema IDs and field metadata remain authoritative.

## Design decisions

- Keep `FieldPack` and `FieldPackSlice`: inline storage and independently allocated storage have different ownership and lifetime requirements.
- Reuse type-erased field callbacks instead of adding special cases to the layout engine. Callback metadata supplies child schema identity without global callback registries.
- Store resolved schema IDs, not schema names or raw registry references. Registry replacement therefore cannot invalidate an existing schema.
- Use explicit `slice` and `pack` Tcl modifiers instead of C++-shaped `&` and `*` syntax. Tcl users see storage behavior directly, without needing C++ ownership terminology.
- Keep nested child access path-based. Returning a borrowed slice as a Tcl handle would allow a dangling handle after parent destruction.