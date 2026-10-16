#
#  SPDX-License-Identifier: GPL-3.0-or-later
#
# Minimal stand-in for Archive::Zip::MemberRead (see Archive/Zip.pm).
# getline() returns the next line without its "\n", like the real module.

package Archive::Zip::MemberRead;

use strict;
use warnings;

sub new {
    my ($class, $zip, $name) = @_;
    my $content = $zip->contents($name);
    return undef unless defined $content;
    my @lines = split /\n/, $content, -1;
    pop @lines if @lines && $lines[-1] eq '';
    return bless { lines => \@lines }, $class;
}

sub getline {
    my ($self) = @_;
    return shift @{$self->{lines}};
}

1;
