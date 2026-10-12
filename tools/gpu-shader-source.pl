#!/usr/bin/env perl
# SPDX-License-Identifier: GPL-3.0-or-later
use strict;
use warnings;
my $name = shift @ARGV;
local $/;
my $source = <>;
$source =~ /const char \*\Q$name\E\s*=\s*((?:"(?:\\.|[^"\\])*"\s*)+);/s
    or die "shader $name not found\n";
my $literals = $1;
while ($literals =~ /"((?:\\.|[^"\\])*)"/g) {
    my $part = $1;
    $part =~ s/\\n/\n/g;
    $part =~ s/\\"/"/g;
    $part =~ s/\\\\/\\/g;
    print $part;
}
