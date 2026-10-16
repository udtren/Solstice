#
#  SPDX-License-Identifier: GPL-3.0-or-later
#
# Minimal stand-in for the Archive::Zip calls extracti18n.pl makes. Git for
# Windows' Perl has no Archive::Zip; update_translations.py puts this
# directory on @INC only when the real module is missing.
#
# The archive is read through its central directory, so members written
# with data descriptors (sizes after the data, as in the SeExpr bundle) work;
# IO::Uncompress::Unzip cannot read those. Only stored and deflated members
# are supported.

package Archive::Zip;

use strict;
use warnings;
use Exporter 'import';
use Compress::Raw::Zlib;

use constant AZ_OK => 0;
use constant AZ_ERROR => 2;

our @EXPORT_OK = qw(AZ_OK AZ_ERROR);
our %EXPORT_TAGS = (ERROR_CODES => [qw(AZ_OK AZ_ERROR)]);

sub new {
    my ($class) = @_;
    return bless { names => [], data => {} }, $class;
}

sub read {
    my ($self, $fileName) = @_;
    open(my $fh, '<:raw', $fileName) or return AZ_ERROR;
    local $/;
    my $zip = <$fh>;
    close($fh);

    # end of central directory record, searched from the end (comment <= 64 KiB)
    my $eocd = rindex($zip, "PK\x05\x06");
    return AZ_ERROR if $eocd < 0;
    my ($count, $cdOffset) = unpack('x10 v x4 V', substr($zip, $eocd, 22));

    my $pos = $cdOffset;
    for (1 .. $count) {
        return AZ_ERROR if substr($zip, $pos, 4) ne "PK\x01\x02";
        my ($method, $compSize, $nameLen, $extraLen, $commentLen, $localOffset) =
            unpack('x10 v x8 V x4 v v v x8 V', substr($zip, $pos, 46));
        my $name = substr($zip, $pos + 46, $nameLen);
        $pos += 46 + $nameLen + $extraLen + $commentLen;

        my ($localNameLen, $localExtraLen) = unpack('x26 v v', substr($zip, $localOffset, 30));
        my $raw = substr($zip, $localOffset + 30 + $localNameLen + $localExtraLen, $compSize);
        my $content;
        if ($method == 0) {
            $content = $raw;
        } elsif ($method == 8) {
            my ($inflater, $status) = Compress::Raw::Zlib::Inflate->new(-WindowBits => -MAX_WBITS);
            return AZ_ERROR unless $inflater;
            $status = $inflater->inflate($raw, $content);
            return AZ_ERROR unless $status == Z_OK or $status == Z_STREAM_END;
        } else {
            return AZ_ERROR;
        }
        push @{$self->{names}}, $name;
        $self->{data}{$name} = $content;
    }
    return AZ_OK;
}

sub memberNames {
    my ($self) = @_;
    return @{$self->{names}};
}

sub contents {
    my ($self, $name) = @_;
    return $self->{data}{$name};
}

1;
