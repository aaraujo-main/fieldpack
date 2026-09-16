if {$argc != 1} { error "usage: fieldpack.tcl extension" }
load [lindex $argv 0] Fieldpack
package require fieldpack 1.0

proc assert_eq {actual expected label} {
    if {$actual ne $expected} { error "$label: expected '$expected', got '$actual'" }
}
proc assert_error {script label} {
    if {[catch {uplevel 1 $script}] == 0} { error "$label: expected error" }
}

# Global schema registry and schema metadata.
set schema [::fieldpack::schema::register {} {int double string bool obj} 1]
assert_eq [::fieldpack::schema::name $schema] {} unnamed_schema
assert_eq [::fieldpack::schema::field_count $schema] 5 field_count
assert_eq [::fieldpack::schema::field_type $schema 2] string field_type
if {[::fieldpack::schema::size $schema] <= 0} { error "invalid schema size" }

set named [::fieldpack::schema::register point {int double} 1]
assert_eq [::fieldpack::schema::name $named] point schema_name
assert_eq [::fieldpack::schema::exists point] 1 named_exists
assert_eq [::fieldpack::schema::id point] $named named_id
assert_eq [::fieldpack::schema::register point {int double} 1] $named named_reuse

set anonymousA [::fieldpack::schema::register {} {int double} 1]
set anonymousB [::fieldpack::schema::register {} {int double} 1]
if {$anonymousA == $anonymousB} { error "anonymous schemas must receive distinct ids" }
set registered [::fieldpack::schema::register Registered {int double} 1]
assert_eq [::fieldpack::schema::register Registered {int double} 1] $registered registered_reuse
assert_error {::fieldpack::schema::register Registered {int} 1 reject} registered_reject
set registeredReplacement [::fieldpack::schema::register Registered {int} 1 replace]
if {$registeredReplacement == $registered} { error "registered replacement must allocate new id" }

set derived [::fieldpack::schema::derive child point {string bool} 1]
assert_eq [::fieldpack::schema::name $derived] child derived_name
assert_eq [::fieldpack::schema::field_count $derived] 4 derived_field_count
assert_eq [::fieldpack::schema::field_type $derived 0] int derived_inherited_int
assert_eq [::fieldpack::schema::field_type $derived 1] double derived_inherited_double
assert_eq [::fieldpack::schema::field_type $derived 2] string derived_new_string
assert_eq [::fieldpack::schema::field_type $derived 3] bool derived_new_bool

if {[catch {::fieldpack::schema::register point {int} 1 reject} message] == 0} {
    error "divergent schema redeclaration should fail"
}
set replaced [::fieldpack::schema::register point {int} 1 replace]
if {$replaced == $named} { error "replacement must allocate new id" }
assert_eq [::fieldpack::schema::id point] $replaced named_replace

set derived_by_name [::fieldpack::schema::derive {} child {obj} 1]
assert_eq [::fieldpack::schema::field_count $derived_by_name] 5 derived_by_name

# Nested schema declarations and storage modes.
set nested [::fieldpack::schema::register nested [list [list slice child] [list pack child]] 1]
assert_eq [::fieldpack::schema::field_type $nested 0] slice nested_slice_type
assert_eq [::fieldpack::schema::field_type $nested 1] pack nested_pack_type
if {[::fieldpack::schema::size $nested] <= [::fieldpack::schema::size $derived_by_name]} {
    error "nested schema size should include child storage"
}
# Nested field access and mutation.
set nestedPack [::fieldpack::new $nested]
::fieldpack::set_path nestedPack {0 0} 71
::fieldpack::set_path nestedPack {1 0} 82
assert_eq [::fieldpack::get_path $nestedPack {0 0}] 71 nested_slice_value
assert_eq [::fieldpack::get_path $nestedPack {1 0}] 82 nested_pack_value
set nestedCopy $nestedPack
::fieldpack::set_path nestedPack {0 0} 72
assert_eq [::fieldpack::get_path $nestedCopy {0 0}] 71 nested_path_copy_on_write
assert_eq [::fieldpack::get_path $nestedPack {0 0}] 72 nested_path_mutation
set nestedSliceValue [::fieldpack::get $nestedPack 0]
assert_eq [::fieldpack::get_schema_id $nestedSliceValue] $derived nested_get_slice_schema
assert_eq [::fieldpack::get $nestedSliceValue 0] 72 nested_get_slice_value
set nestedPackValue [::fieldpack::get $nestedPack 1]
assert_eq [::fieldpack::get_schema_id $nestedPackValue] $derived nested_get_pack_schema
assert_eq [::fieldpack::get $nestedPackValue 0] 82 nested_get_pack_value
::fieldpack::set nestedSliceValue 0 73
::fieldpack::set nestedPack 0 $nestedSliceValue
assert_eq [::fieldpack::get_path $nestedPack {0 0}] 73 nested_set_slice
::fieldpack::set nestedPackValue 0 83
::fieldpack::set nestedPack 1 $nestedPackValue
assert_eq [::fieldpack::get_path $nestedPack {1 0}] 83 nested_set_pack
set nestedSetCopy $nestedPack
::fieldpack::set nestedSliceValue 0 74
::fieldpack::set nestedPack 0 $nestedSliceValue
assert_eq [::fieldpack::get_path $nestedSetCopy {0 0}] 73 nested_set_copy_on_write
assert_eq [::fieldpack::get_path $nestedPack {0 0}] 74 nested_set_value
set wrongNestedValue [::fieldpack::new $schema]
assert_error {::fieldpack::set nestedPack 0 $wrongNestedValue} nested_set_wrong_schema
set numericNested [::fieldpack::schema::register {} [list [list slice $derived_by_name]] 1]
assert_eq [::fieldpack::schema::field_type $numericNested 0] slice numeric_nested_type
assert_error {::fieldpack::schema::register {} {{slice missing_schema}}} missing_nested_name
assert_error {::fieldpack::schema::register {} {{pack 999999}}} missing_nested_id

set outer [::fieldpack::schema::register outer [list [list slice nested] [list pack nested]] 1]
set outerPack [::fieldpack::new $outer]
::fieldpack::set_path outerPack {0 0 0} 91
::fieldpack::set_path outerPack {0 1 0} 92
::fieldpack::set_path outerPack {1 0 0} 93
::fieldpack::set_path outerPack {1 1 0} 94
assert_eq [::fieldpack::get_path $outerPack {0 0 0}] 91 nested_slice_slice_value
assert_eq [::fieldpack::get_path $outerPack {0 1 0}] 92 nested_slice_pack_value
assert_eq [::fieldpack::get_path $outerPack {1 0 0}] 93 nested_pack_slice_value
assert_eq [::fieldpack::get_path $outerPack {1 1 0}] 94 nested_pack_pack_value
assert_error {::fieldpack::get_path $outerPack {0 0 9}} nested_bad_leaf_index
assert_eq [::fieldpack::get_schema_id [::fieldpack::get_path $outerPack {0 1}]] $derived nested_nested_get

set stringChild [::fieldpack::schema::register StringChild {int string} 1]
set printable [::fieldpack::schema::register Printable [list [list slice $stringChild] [list pack $stringChild]] 1]
set printablePack [::fieldpack::new $printable]
assert_eq [::fieldpack::get_schema_name $printablePack] Printable named_pack_schema_name
assert_eq [::fieldpack::get_schema_id $printablePack] $printable named_pack_schema_id
::fieldpack::set_path printablePack {0 0} 2
::fieldpack::set_path printablePack {0 1} {slice words}
::fieldpack::set_path printablePack {1 0} 3
::fieldpack::set_path printablePack {1 1} {pack words}
assert_eq $printablePack {Printable {StringChild 2 {slice words}} {StringChild 3 {pack words}}} to_string
set roundTripText {Printable {StringChild 2 {slice words}} {StringChild 3 {pack words}}}
assert_eq [::fieldpack::get_path $roundTripText {0 1}] {slice words} from_string_slice
assert_eq [::fieldpack::get_path $roundTripText {1 1}] {pack words} from_string_pack
assert_error {::fieldpack::get_path {Missing 1} {0}} from_string_missing_schema

# Flat field access, copy-on-write, and conversion errors.
set pack [::fieldpack::new $schema]
assert_eq [::fieldpack::get_schema_name $pack] {} pack_schema_name
assert_eq [::fieldpack::get_schema_id $pack] $schema pack_schema_id
::fieldpack::set pack 0 42
::fieldpack::set pack 1 3.25
::fieldpack::set pack 2 hello
::fieldpack::set pack 3 true
set objectValue [list retained value]
::fieldpack::set pack 4 $objectValue
assert_eq [::fieldpack::get $pack 0] 42 int
assert_eq [::fieldpack::get $pack 1] 3.25 double
assert_eq [::fieldpack::get $pack 2] hello string
assert_eq [::fieldpack::get $pack 3] 1 bool
assert_eq [::fieldpack::get $pack 4] $objectValue obj

set copy $pack
::fieldpack::set pack 2 changed
assert_eq [::fieldpack::get $copy 2] hello copy_on_write
assert_eq [::fieldpack::get $pack 2] changed copy_on_write
assert_error {::fieldpack::get $pack 99} bad_index
assert_error {::fieldpack::set pack 0 nope} bad_int

# Reference-based field updates.
set updatePack [::fieldpack::new $schema]
::fieldpack::set updatePack 0 10
::fieldpack::set updatePack 1 2.5
::fieldpack::set updatePack 3 false
::fieldpack::set updatePack 4 {retained value}
set updateTemp sentinel
::fieldpack::update updatePack 0 updateTemp { incr updateTemp 5 }
assert_eq [::fieldpack::get $updatePack 0] 15 update_int
assert_eq $updateTemp {} update_int_cleanup
::fieldpack::update updatePack 1 updateTemp { set updateTemp [expr {$updateTemp * 2.0}] }
assert_eq [::fieldpack::get $updatePack 1] 5.0 update_double
::fieldpack::update updatePack 3 updateTemp { set updateTemp true }
assert_eq [::fieldpack::get $updatePack 3] 1 update_bool
::fieldpack::update updatePack 2 updateTemp { append updateTemp -updated }
assert_eq [::fieldpack::get $updatePack 2] -updated update_string
::fieldpack::update updatePack 4 updateTemp { lappend updateTemp changed }
assert_eq [::fieldpack::get $updatePack 4] {retained value changed} update_obj
assert_eq $updateTemp {} update_obj_cleanup

::fieldpack::update nestedPack 0 updateTemp { ::fieldpack::set updateTemp 0 75 }
assert_eq [::fieldpack::get_path $nestedPack {0 0}] 75 nested_update_slice
assert_eq $updateTemp {} nested_update_slice_cleanup
::fieldpack::update nestedPack 1 updateTemp { ::fieldpack::set updateTemp 0 84 }
assert_eq [::fieldpack::get_path $nestedPack {1 0}] 84 nested_update_pack
assert_eq $updateTemp {} nested_update_pack_cleanup
set nestedUpdateCopy $nestedPack
::fieldpack::update nestedPack 0 updateTemp { ::fieldpack::set updateTemp 0 76 }
assert_eq [::fieldpack::get_path $nestedPack {0 0}] 76 nested_update_copy_value
assert_eq [::fieldpack::get_path $nestedUpdateCopy {0 0}] 75 nested_update_copy_preserved
if {[catch {::fieldpack::update nestedPack 0 updateTemp {
    ::fieldpack::set updateTemp 0 77
    error nested-update-body-error
}} updateMessage] == 0} { error "nested update body error should propagate" }
assert_eq $updateMessage nested-update-body-error nested_update_body_error
assert_eq [::fieldpack::get_path $nestedPack {0 0}] 77 nested_update_body_writeback
assert_eq $updateTemp {} nested_update_body_cleanup

set updateCopy $updatePack
::fieldpack::update updatePack 0 updateTemp { incr updateTemp 10 }
assert_eq [::fieldpack::get $updatePack 0] 25 update_copy_value
assert_eq [::fieldpack::get $updateCopy 0] 15 update_copy_preserved

if {[catch {::fieldpack::update updatePack 0 updateTemp {
    set updateTemp 31
    error update-body-error
}} updateMessage] == 0} { error "update body error should propagate" }
assert_eq $updateMessage update-body-error update_body_error
assert_eq [::fieldpack::get $updatePack 0] 31 update_body_writeback
assert_eq $updateTemp {} update_body_cleanup

assert_error {::fieldpack::update updatePack 0 updateTemp { unset updateTemp }} update_undefined
assert_eq $updateTemp {} update_undefined_cleanup
assert_eq [::fieldpack::get $updatePack 0] 31 update_undefined_preserved
assert_error {::fieldpack::update updatePack 0 updateTemp { set updateTemp invalid }} update_conversion
assert_eq $updateTemp {} update_conversion_cleanup
assert_eq [::fieldpack::get $updatePack 0] 31 update_conversion_preserved
assert_error {::fieldpack::update updatePack -1 updateTemp {}} update_negative_index
assert_error {::fieldpack::update updatePack 99 updateTemp {}} update_bad_index
assert_error {::fieldpack::update updatePack 0 updateTemp} update_bad_arg_count
assert_error {::fieldpack::update missingPack 0 updateTemp {}} update_missing_pack
puts "fieldpack Tcl tests passed"
