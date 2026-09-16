#!/usr/bin/env tclsh
#
# install.tcl - Install the FieldPack package into the current Tcl installation.
#
# Usage:
#   tclsh scripts/install.tcl ?build-directory?
#

set scriptDir [file dirname [file normalize [info script]]]
set repoDir [file dirname $scriptDir]
if {$argc > 1} {
    puts stderr "Usage: tclsh scripts/install.tcl ?build-directory?"
    exit 2
}
set buildDir [file normalize [expr {$argc == 0 ? [file join $repoDir build] : [lindex $argv 0]}]]

set packageName fieldpack
set packageVersion 1.0.0
set extensionName "${packageName}[info sharedlibextension]"
set pkgDir "${packageName}${packageVersion}"

# Determine the Tcl library directory
set libDir [info library]
set installDir [file join [file dirname $libDir] $pkgDir]

# Find package files relative to this script
set extensionSrc [file join $buildDir $extensionName]
set pkgSrc [file join $repoDir tcl pkgIndex.tcl]

# Validate source files exist
foreach f [list $extensionSrc $pkgSrc] {
    if {![file exists $f]} {
        puts stderr "Error: source file not found: $f"
        exit 1
    }
}

# Create the installation directory
if {[catch {file mkdir $installDir} err]} {
    puts stderr "Error: could not create directory '$installDir': $err"
    puts stderr "You may need to run this script with elevated privileges (e.g. sudo)."
    exit 1
}

# Copy files
foreach {src name} [list $extensionSrc $extensionName $pkgSrc pkgIndex.tcl] {
    set dst [file join $installDir $name]
    if {[catch {file copy -force $src $dst} err]} {
        puts stderr "Error: could not copy '$name' to '$installDir': $err"
        exit 1
    }
}

puts "FieldPack $packageVersion installed successfully to:"
puts "  $installDir"
puts ""
puts "You can now use it in Tcl with:"
puts "  package require $packageName $packageVersion"
