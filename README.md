# FieldPack - Compact Buffer Layouts for Runtime-Defined Objects

FieldPack defines compact buffer layouts for objects built from runtime-defined class schemas. Each schema describes field types, sizes, alignment, and memory positions. Each object uses one contiguous buffer for its schema ID and field data, while schema metadata handles field access and lifecycle without generated C++ classes.

The project also provides a Tcl extension with an API for runtime schema registration, copy-on-write values, scalar access, and nested `slice` or `pack` fields.

In local Tcl benchmarks using 100,000 operations or values, FieldPack **reads** are **1.14-1.17x** faster than lists and **1.97-2.04x** faster than dicts; **writes** are **0.86-0.96x** as fast as lists and **1.72-2.16x** faster than dicts. FieldPack uses **1.40-3.10x less peak memory** than lists and **3.53-11.68x less** than dicts.

# Data Layout

Each field is stored in a single byte array, including the schema ID, inline fields, inline nested field packs, and fields that point to separately allocated data.

```
╔═══════════╗───────┬─────────────────────┬─────────┬─────┐ 
║ schema ID ║ field │ [ subfield |  ... ] │ pointer │ ... │
╚═══════════╝───────┴─────────────────────┴─────────┴─────┘
       │                                        │
       ▼                                        ▼
╔═══════════════╗                     ┌──────────────────┐ 
║ schema layout ║                     │ field allocation │
╚═══════════════╝                     └──────────────────┘
```


## Contents

- [Quickstart](#quickstart)
- [Install](#install)
- [Tcl API](#tcl-api)
- [Benchmarks](#benchmarks)
- [Build](#build)
- [Tests](#tests)
- [Architecture](#architecture)
- [License](#license)

## Quickstart

Build the extension, then load it directly from Tcl:

```sh
cmake -S . -B build -DFIELDPACK_BUILD_TESTS=ON
cmake --build build
tclsh
```

```tcl
load ./build/fieldpack[info sharedlibextension] Fieldpack
package require fieldpack 1.0

# Register a schema and inspect its metadata.
set schema [fieldpack::schema::register Point {int double string} 1]
puts [fieldpack::schema::name $schema]
puts [fieldpack::schema::field_count $schema]
puts [fieldpack::schema::field_type $schema 1]

# Create and access a FieldPack value.
set point [fieldpack::new $schema]
fieldpack::set point 0 42
fieldpack::set point 1 2.5
fieldpack::set point 2 "hello world"
puts [fieldpack::get $point 2]

# Tcl assignment shares value; mutation triggers copy-on-write.
set copy $point
fieldpack::set point 2 changed
puts [fieldpack::get $copy 2]
```

`package require fieldpack 1.0` accepts installed version `1.0.0`.

## Install

### CMake install

Install the extension, package index, static core library, and documentation:

```sh
cmake --install build --prefix "$HOME/.local"
```

Add the installed `lib` directory to the Tcl package search path when needed:

```tcl
lappend auto_path [file normalize "$::env(HOME)/.local/lib"]
package require fieldpack 1.0
```

### Tcl helper script

`scripts/install.tcl` copies the built extension and package index into the active Tcl installation:

```sh
tclsh scripts/install.tcl
tclsh scripts/install.tcl build-debug
```

Default build directory is `build`. Elevated permissions may be required for system Tcl directories.

## Tcl API

Load the extension before using its commands:

```tcl
package require fieldpack 1.0
```

### Field types

Scalar field types:

- `int`: signed 32-bit integer.
- `double`: C++ `double`.
- `bool`: Tcl boolean backed by C++ `bool`.
- `string`: C++ `std::string`.
- `obj`: retained `Tcl_Obj*`, preserving Tcl value semantics.

Nested fields use two-element descriptors:

```tcl
{slice SchemaName}
{pack SchemaName}
```

`slice` stores child fields inline in the parent storage. `pack` stores independently allocated child storage owned by the parent.

### Schema commands

#### `fieldpack::schema::register name types ?optimize? ?redeclare?`

Registers a schema and returns its numeric schema ID. An empty `name` creates a fresh anonymous schema. `types` is a list of field types or nested descriptors. `optimize` is a boolean; when true, it sorts physical placement by alignment while preserving logical indexes.

`redeclare` applies to named schemas:

- `reuse-equivalent` (default): reuse ID when definition matches.
- `reject`: reject any existing name.
- `replace`: allocate new ID and remap name; existing IDs remain valid.

```tcl
set point [fieldpack::schema::register Point {int double} 1]
set line [fieldpack::schema::register Line {{slice Point} {pack Point}} 1]
```

#### `fieldpack::schema::derive name base types ?optimize? ?redeclare?`

Creates a schema by appending `types` to `base`. `base` accepts a numeric schema ID or a registered name. An empty `name` creates an anonymous schema. An omitted `optimize` value inherits the base setting.

```tcl
set child [fieldpack::schema::derive Child Point {string bool}]
```

#### `fieldpack::schema::exists name`

Returns a boolean indicating whether a named schema exists.

```tcl
if {[fieldpack::schema::exists Point]} { puts "Point exists" }
```

#### `fieldpack::schema::id name`

Returns the numeric ID for a registered name. Reports an error if the name is unknown.

```tcl
set pointId [fieldpack::schema::id Point]
```

#### `fieldpack::schema::field_count schemaId`

Returns the logical field count.

#### `fieldpack::schema::field_type schemaId index`

Returns the field type at logical `index`. Nested fields return `slice` or `pack`.

#### `fieldpack::schema::size schemaId`

Returns the schema storage size in bytes. This includes inline child storage and `pack` object storage, but not separately allocated child buffers.

#### `fieldpack::schema::name schemaId`

Returns the registered name. Anonymous schemas return an empty string.

```tcl
puts [fieldpack::schema::field_count $point]
puts [fieldpack::schema::field_type $point 0]
puts [fieldpack::schema::size $point]
puts [fieldpack::schema::name $point]
```

### FieldPack commands

#### `fieldpack::new schemaId`

Creates a FieldPack value for a schema ID.

```tcl
set point [fieldpack::new $pointId]
```

#### `fieldpack::get_schema_name pack`

Returns the schema name for a FieldPack value. Anonymous schemas return an empty string.

#### `fieldpack::get_schema_id pack`

Returns the numeric schema ID for a FieldPack value.

#### `fieldpack::get pack index`

Reads field at a logical index. Nested fields return a FieldPack value.

#### `fieldpack::set packVarName index value`

Sets field at a logical index. Nested values must use destination field schema. The first
argument is a variable name, allowing copy-on-write replacement.

```tcl
fieldpack::set point 0 10
puts [fieldpack::get $point 0]
```

#### `fieldpack::update packVarName fieldIndex fieldVarName body`

Binds a scalar or nested FieldPack field to `fieldVarName`, evaluates `body` in the current Tcl scope,
and writes the final field variable value back. The variable name is passed without
`$`; it is cleared after the command. Copy-on-write replacement applies to
`packVarName`, and field changes are written back when the body returns an error.
Nested fields bind as FieldPack values and can be mutated with `fieldpack::set`.
Nested values must match destination schema.

```tcl
fieldpack::update point 0 x { set x [expr {$x + 1}] }
```

#### `fieldpack::get_path pack path`

Reads the final scalar field through nested logical indexes. `path` is a non-empty list of indexes.

#### `fieldpack::set_path packVarName path value`

Sets the final scalar field through nested logical indexes. The variable-name argument preserves copy-on-write behavior.

```tcl
set lineValue [fieldpack::new $line]
fieldpack::set_path lineValue {0 0} 10
fieldpack::set_path lineValue {1 1} 20
puts [fieldpack::get_path $lineValue {0 0}]
```

Paths may traverse arbitrary nesting. Indexes are logical indexes, independent of alignment-optimized physical offsets. Direct `get`, `set`, and `update` support complete nested FieldPack values; use `get_path` and `set_path` for leaf fields.

### Tcl value representation

FieldPack values are converted to Tcl lists:

```text
SchemaName scalarValue ... {ChildSchema nestedValue ...}
```

Tcl assignment shares values. Mutating through `set` or `set_path` copies shared storage first, so aliases remain independent. Schema IDs and schemas remain valid for process lifetime.

## Benchmarks

### Tcl access time

```sh
tclsh benchmark/fieldpack_time.tcl build/fieldpack.so 100000
```

Compares FieldPack access with access to Tcl lists and dictionaries.

### Native C++ access time

```sh
cmake -S . -B build-cpp-benchmark -DFIELDPACK_BUILD_BENCHMARKS=ON
cmake --build build-cpp-benchmark --target fieldpack_cpp_time
build-cpp-benchmark/fieldpack_cpp_time 100000
```

Measures native scalar and nested `slice`/`pack` access.

### Memory

```sh
tclsh benchmark/fieldpack_mem.tcl fieldpack 100000 scalar build/fieldpack.so
tclsh benchmark/fieldpack_mem.tcl list 100000 scalar
tclsh benchmark/fieldpack_mem.tcl dict 100000 scalar
```

Scenarios are `scalar`, `simple`, `many`, `strings`, and `nested`. Start a fresh process for each representation. The script waits for Enter so resident memory can be inspected with `htop`.

### Recorded results

Measured on Linux with Tcl 8.6, GCC 11, and 100,000 iterations or values. Speedup and memory advantage are baseline / FieldPack; `1.71x` means FieldPack uses 1.71x less. Values above `1.00x` favor FieldPack.

#### Time

Timing uses equivalent FieldPack, list, and dict fixtures, with setup outside loops, 1,000 warmup iterations, and 100,000 timed iterations.

| Operation | FieldPack (us/op) | vs list (x) | vs dict (x) |
| --- | ---: | ---: | ---: |
| Scalar read | 0.183 | 1.15x | 2.04x |
| Scalar write | 0.378 | 0.86x | 1.72x |
| Nested slice read | 0.199 | 1.17x | 1.97x |
| Nested slice write | 0.360 | 0.96x | 2.16x |
| Nested pack read | 0.205 | 1.14x | 1.99x |
| Nested pack write | 0.356 | 0.90x | 2.03x |

**Scalar read.** Reads integer index `0` with `fieldpack::get`, `lindex`, or `dict get`.

**Scalar write.** Writes integer index `0` with `fieldpack::set`, `lset`, or `dict set`.

**Nested slice read.** Reads inline child integer at `{0 0}` with `fieldpack::get_path`, `lindex`, or `dict get`.

**Nested slice write.** Writes inline child integer at `{0 0}` with `fieldpack::set_path`, `lset`, or `dict set`.

**Nested pack read.** Reads allocated child integer at `{1 0}` with `fieldpack::get_path`, `lindex`, or `dict get`.

**Nested pack write.** Writes allocated child integer at `{1 0}` with `fieldpack::set_path`, `lset`, or `dict set`.

#### Memory

Memory uses 100,000 values per fresh process. Peak RSS comes from `/usr/bin/time` after construction.

| Scenario | FieldPack peak RSS (MiB) | vs list (x) | vs dict (x) |
| --- | ---: | ---: | ---: |
| Scalar | 17.3 | 1.71x | 4.88x |
| Simple | 20.4 | 1.76x | 4.46x |
| Many fields | 29.7 | 3.10x | 11.68x |
| Strings | 76.8 | 1.40x | 3.53x |
| Nested | 29.7 | 2.49x | 7.63x |

**Simple.** Each value has one integer, double, and string.

**Scalar.** Each value has one integer, double, and boolean; no strings or nesting.

**Many fields.** Each value has 12 repeating integer, double, string, and boolean fields.

**Strings.** Each value has eight generated string fields.

**Nested.** Each value has two integer/double/string children: one inline `slice`, one allocated `pack`.

Results are one local run, not a performance guarantee. Repeat on target hardware before making deployment capacity decisions.

## Build

Requirements include CMake 3.16+, Tcl 8.6+, and a C++17 compiler.

```sh
cmake -S . -B build -DFIELDPACK_BUILD_TESTS=ON
cmake --build build
```

Build without tests:

```sh
cmake -S . -B build-cpp-off -DFIELDPACK_BUILD_TESTS=OFF
cmake --build build-cpp-off
```

## Tests

Run all configured tests with:

```sh
ctest --test-dir build --output-on-failure
```

Run individual tests with:

```sh
ctest --test-dir build -R fieldpack_core --output-on-failure
ctest --test-dir build -R fieldpack_tcl --output-on-failure
```

Run the Tcl test directly with the extension path:

```sh
tclsh tests/fieldpack.tcl build/fieldpack.so
```

## Architecture

Architecture, ownership rules, storage policies, lifecycle invariants, and Tcl representation details are documented in [docs/architecture.md](docs/architecture.md).

Core dependency direction:

```text
FieldPack -> Registry -> Schema -> FieldOps
```

## License

See [LICENSE.md](LICENSE.md) for license details.
