if {![package vsatisfies [package provide Tcl] 8.6]} { return }
package ifneeded fieldpack 1.0.0 [list load [file join $dir fieldpack[info sharedlibextension]] Fieldpack]
