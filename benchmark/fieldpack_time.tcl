if {$argc < 1 || $argc > 2} {
    error "usage: fieldpack_time.tcl extension ?iterations?"
}

set extension [lindex $argv 0]
set iterations 100000
if {$argc == 2} {
    if {![string is integer -strict [lindex $argv 1]] || [lindex $argv 1] < 1} {
        error "iterations must be positive integer"
    }
    set iterations [lindex $argv 1]
}

load $extension Fieldpack
package require fieldpack 1.0

proc assert_equal {actual expected label} {
    if {$actual ne $expected} {
        error "$label: expected '$expected', got '$actual'"
    }
}

proc benchmark {label script iterations} {
    set warmup [expr {$iterations < 1000 ? $iterations : 1000}]
    for {set index 0} {$index < $warmup} {incr index} {
        uplevel 1 $script
    }
    set result [uplevel 1 [list time $script $iterations]]
    set microseconds [lindex $result 0]
    puts [format "%-30s %12.3f us/op" $label $microseconds]
    return $microseconds
}

proc report_speedup {fieldpack_time baseline_time} {
    if {$fieldpack_time == 0.0} {
        return "inf"
    }
    return [format "%.2fx" [expr {$baseline_time / $fieldpack_time}]]
}

proc run_comparison {label fieldpack_script list_script dict_script iterations} {
    set fieldpack_time [uplevel 1 [list benchmark "$label / fieldpack" $fieldpack_script $iterations]]
    set list_time [uplevel 1 [list benchmark "$label / list" $list_script $iterations]]
    set dict_time [uplevel 1 [list benchmark "$label / dict" $dict_script $iterations]]
    puts [format "%-30s %12s" "$label / list speedup" [report_speedup $fieldpack_time $list_time]]
    puts [format "%-30s %12s" "$label / dict speedup" [report_speedup $fieldpack_time $dict_time]]
    puts ""
}

puts "FieldPack field-access benchmark"
puts "iterations: $iterations"
puts "speedup = baseline time / FieldPack time; >1.00x favors FieldPack"
puts ""

# Keep setup outside timed bodies. Both fixtures hold same logical values.
set scalar_schema [::fieldpack::schema::register {} {int double string} 1]
set scalar_pack [::fieldpack::new $scalar_schema]
::fieldpack::set scalar_pack 0 17
::fieldpack::set scalar_pack 1 2.5
::fieldpack::set scalar_pack 2 benchmark
set scalar_list [list 17 2.5 benchmark]
set scalar_dict [dict create 0 17 1 2.5 2 benchmark]

assert_equal [::fieldpack::get $scalar_pack 0] [lindex $scalar_list 0] scalar_read
assert_equal [::fieldpack::get $scalar_pack 0] [dict get $scalar_dict 0] scalar_dict_read
::fieldpack::set scalar_pack 0 19
lset scalar_list 0 19
dict set scalar_dict 0 19
assert_equal [::fieldpack::get $scalar_pack 0] [lindex $scalar_list 0] scalar_write
assert_equal [::fieldpack::get $scalar_pack 0] [dict get $scalar_dict 0] scalar_dict_write
::fieldpack::set scalar_pack 0 17
lset scalar_list 0 17
dict set scalar_dict 0 17

run_comparison "scalar read" [list ::fieldpack::get $scalar_pack 0] [list lindex $scalar_list 0] \
    [list dict get $scalar_dict 0] $iterations
run_comparison "scalar write" {::fieldpack::set scalar_pack 0 19} {lset scalar_list 0 19} \
    {dict set scalar_dict 0 19} $iterations

set child_schema [::fieldpack::schema::register {} {int double} 1]
set nested_schema [::fieldpack::schema::register {} [list [list slice $child_schema] [list pack $child_schema]] 1]
set nested_pack [::fieldpack::new $nested_schema]
::fieldpack::set_path nested_pack {0 0} 31
::fieldpack::set_path nested_pack {0 1} 3.5
::fieldpack::set_path nested_pack {1 0} 47
::fieldpack::set_path nested_pack {1 1} 4.5
set nested_list [list [list 31 3.5] [list 47 4.5]]
set nested_dict [dict create 0 [dict create 0 31 1 3.5] 1 [dict create 0 47 1 4.5]]

assert_equal [::fieldpack::get_path $nested_pack {0 0}] [lindex $nested_list 0 0] slice_read
assert_equal [::fieldpack::get_path $nested_pack {1 0}] [lindex $nested_list 1 0] pack_read
assert_equal [::fieldpack::get_path $nested_pack {0 0}] [dict get $nested_dict 0 0] slice_dict_read
assert_equal [::fieldpack::get_path $nested_pack {1 0}] [dict get $nested_dict 1 0] pack_dict_read
::fieldpack::set_path nested_pack {0 0} 37
::fieldpack::set_path nested_pack {1 0} 53
lset nested_list 0 0 37
lset nested_list 1 0 53
dict set nested_dict 0 0 37
dict set nested_dict 1 0 53
assert_equal [::fieldpack::get_path $nested_pack {0 0}] [lindex $nested_list 0 0] slice_write
assert_equal [::fieldpack::get_path $nested_pack {1 0}] [lindex $nested_list 1 0] pack_write
assert_equal [::fieldpack::get_path $nested_pack {0 0}] [dict get $nested_dict 0 0] slice_dict_write
assert_equal [::fieldpack::get_path $nested_pack {1 0}] [dict get $nested_dict 1 0] pack_dict_write
::fieldpack::set_path nested_pack {0 0} 31
::fieldpack::set_path nested_pack {1 0} 47
lset nested_list 0 0 31
lset nested_list 1 0 47
dict set nested_dict 0 0 31
dict set nested_dict 1 0 47

run_comparison "nested slice read" [list ::fieldpack::get_path $nested_pack {0 0}] \
    [list lindex $nested_list 0 0] [list dict get $nested_dict 0 0] $iterations
run_comparison "nested slice write" {::fieldpack::set_path nested_pack {0 0} 37} \
    {lset nested_list 0 0 37} {dict set nested_dict 0 0 37} $iterations

run_comparison "nested pack read" [list ::fieldpack::get_path $nested_pack {1 0}] \
    [list lindex $nested_list 1 0] [list dict get $nested_dict 1 0] $iterations
run_comparison "nested pack write" {::fieldpack::set_path nested_pack {1 0} 53} \
    {lset nested_list 1 0 53} {dict set nested_dict 1 0 53} $iterations