if {$argc < 3 || $argc > 4} {
    error "usage: fieldpack_mem.tcl fieldpack|list|dict N scalar|simple|many|strings|nested ?extension?"
}

set implementation [lindex $argv 0]
set count [lindex $argv 1]
set scenario [lindex $argv 2]
if {$implementation ni {fieldpack list dict}} {
    error "implementation must be fieldpack, list, or dict"
}
if {![string is integer -strict $count] || $count < 1} {
    error "N must be positive integer"
}
if {$scenario ni {scalar simple many strings nested}} {
    error "scenario must be scalar, simple, many, strings, or nested"
}
if {$implementation eq "fieldpack"} {
    set extension [expr {$argc == 4 ? [lindex $argv 3] : "./fieldpack[info sharedlibextension]"}]
    load $extension Fieldpack
    package require fieldpack 1.0
}

proc make_value {implementation scenario index schemas} {
    if {$implementation eq "fieldpack"} {
        if {$scenario eq "scalar"} {
            set value [::fieldpack::new [dict get $schemas scalar]]
            ::fieldpack::set value 0 $index
            ::fieldpack::set value 1 [expr {$index + 0.5}]
            ::fieldpack::set value 2 [expr {$index % 2}]
            return $value
        }
        if {$scenario eq "simple"} {
            set value [::fieldpack::new [dict get $schemas simple]]
            ::fieldpack::set value 0 $index
            ::fieldpack::set value 1 [expr {$index + 0.5}]
            ::fieldpack::set value 2 "value-$index"
            return $value
        }
        if {$scenario eq "many"} {
            set value [::fieldpack::new [dict get $schemas many]]
            for {set field 0} {$field < 12} {incr field} {
                switch [expr {$field % 4}] {
                    0 { set field_value $index }
                    1 { set field_value [expr {$index + 0.5}] }
                    2 { set field_value "value-$index" }
                    3 { set field_value [expr {$index % 2}] }
                }
                ::fieldpack::set value $field $field_value
            }
            return $value
        }
        if {$scenario eq "strings"} {
            set value [::fieldpack::new [dict get $schemas strings]]
            for {set field 0} {$field < 8} {incr field} {
                ::fieldpack::set value $field "field${field}-value-$index"
            }
            return $value
        }
        set value [::fieldpack::new [dict get $schemas nested]]
        ::fieldpack::set_path value {0 0} $index
        ::fieldpack::set_path value {0 1} [expr {$index + 0.5}]
        ::fieldpack::set_path value {0 2} "left-$index"
        ::fieldpack::set_path value {1 0} [expr {$index * 2}]
        ::fieldpack::set_path value {1 1} [expr {$index + 1.5}]
        ::fieldpack::set_path value {1 2} "right-$index"
        return $value
    }

    if {$scenario eq "scalar"} {
        if {$implementation eq "list"} {
            return [list $index [expr {$index + 0.5}] [expr {$index % 2}]]
        }
        return [dict create int $index double [expr {$index + 0.5}] bool [expr {$index % 2}]]
    }
    if {$scenario eq "simple"} {
        if {$implementation eq "list"} {
            return [list $index [expr {$index + 0.5}] "value-$index"]
        }
        return [dict create int $index double [expr {$index + 0.5}] string "value-$index"]
    }
    if {$scenario eq "many"} {
        set fields [list]
        for {set field 0} {$field < 12} {incr field} {
            switch [expr {$field % 4}] {
                0 { lappend fields $index }
                1 { lappend fields [expr {$index + 0.5}] }
                2 { lappend fields "value-$index" }
                3 { lappend fields [expr {$index % 2}] }
            }
        }
        if {$implementation eq "list"} {
            return $fields
        }
        set result [dict create]
        for {set field 0} {$field < 12} {incr field} {
            dict set result $field [lindex $fields $field]
        }
        return $result
    }
    if {$scenario eq "strings"} {
        set fields [list]
        for {set field 0} {$field < 8} {incr field} {
            lappend fields "field${field}-value-$index"
        }
        if {$implementation eq "list"} {
            return $fields
        }
        set result [dict create]
        for {set field 0} {$field < 8} {incr field} {
            dict set result $field [lindex $fields $field]
        }
        return $result
    }
    if {$implementation eq "list"} {
        return [list [list $index [expr {$index + 0.5}] "left-$index"] \
            [list [expr {$index * 2}] [expr {$index + 1.5}] "right-$index"]]
    }
    return [dict create left [dict create int $index double [expr {$index + 0.5}] string "left-$index"] \
        right [dict create int [expr {$index * 2}] double [expr {$index + 1.5}] string "right-$index"]]
}

set schemas [dict create]
if {$implementation eq "fieldpack"} {
    set scalar_schema [::fieldpack::schema::register {} {int double bool} 1]
    set simple_schema [::fieldpack::schema::register {} {int double string} 1]
    set many_schema [::fieldpack::schema::register {} \
        {int double string bool int double string bool int double string bool} 1]
    set strings_schema [::fieldpack::schema::register {} \
        {string string string string string string string string} 1]
    set nested_child_schema [::fieldpack::schema::register {} {int double string} 1]
    set nested_schema [::fieldpack::schema::register {} \
        [list [list slice $nested_child_schema] [list pack $nested_child_schema]] 1]
    dict set schemas scalar $scalar_schema
    dict set schemas simple $simple_schema
    dict set schemas many $many_schema
    dict set schemas strings $strings_schema
    dict set schemas nested $nested_schema
}

set values [list]
for {set index 0} {$index < $count} {incr index} {
    lappend values [make_value $implementation $scenario $index $schemas]
}

puts "Created $count $implementation $scenario values."
puts "Process remains alive for memory inspection. Press Enter to finish."
flush stdout
gets stdin