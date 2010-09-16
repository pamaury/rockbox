#!/usr/bin/perl
#             __________               __   ___.
#   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
#   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
#   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
#   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
#                     \/            \/     \/    \/            \/
# $Id: wpsbuild.pl 24813 2010-02-21 19:10:57Z kugel $
#

use strict;
use Getopt::Long qw(:config pass_through);	# pass_through so not confused by -DTYPE_STUFF

my $ROOT="..";
my $verbose;
my $rbdir=".rockbox";
my $wpslist;
my $target;
my $modelname;

# Get options
GetOptions ( 'r|root=s'		=> \$ROOT,
	     'm|modelname=s'	=> \$modelname,
	     'v|verbose'	=> \$verbose,
	     'rbdir=s'          => \$rbdir, # If we want to put in a different directory
    );

($wpslist, $target) = @ARGV;

my $firmdir="$ROOT/firmware";
my $cppdef = $target;
my @depthlist = ( 16, 8, 4, 2, 1 );


# LCD sizes
my ($main_height, $main_width, $main_depth);
my ($remote_height, $remote_width, $remote_depth);
my $has_remote;


if(!$wpslist) {
    print "Usage: buildtheme.pl <WPSLIST> <target>\n",
    "Run this script in the root of the target build, and it will put all the\n",
    "stuff in $rbdir/wps/\n";
    exit;
}

sub getlcdsizes
{
    my ($remote) = @_;

    open(GCC, ">gcctemp");
    if($remote) {
        # Get the remote LCD screen size
    print GCC <<STOP
\#include "config.h"
#ifdef HAVE_REMOTE_LCD
Height: LCD_REMOTE_HEIGHT
Width: LCD_REMOTE_WIDTH
Depth: LCD_REMOTE_DEPTH
#endif
STOP
;
    }
    else {
    print GCC <<STOP
\#include "config.h"
Height: LCD_HEIGHT
Width: LCD_WIDTH
Depth: LCD_DEPTH
STOP
;
}
    close(GCC);

    my $c="cat gcctemp | gcc $cppdef -I. -I$firmdir/export -E -P -";

    #print "CMD $c\n";

    open(GETSIZE, "$c|");

    my ($height, $width, $depth);
    while(<GETSIZE>) {
        if($_ =~ /^Height: (\d*)/) {
            $height = $1;
        }
        elsif($_ =~ /^Width: (\d*)/) {
            $width = $1;
        }
        elsif($_ =~ /^Depth: (\d*)/) {
            $depth = $1;
        }
        if($height && $width && $depth) {
            last;
        }
    }
    close(GETSIZE);
    unlink("gcctemp");

    return ($height, $width, $depth);
}

# Get the LCD sizes first
($main_height, $main_width, $main_depth) = getlcdsizes();
($remote_height, $remote_width, $remote_depth) = getlcdsizes(1);

#print "LCD: ${main_width}x${main_height}x${main_depth}\n";
$has_remote = 1 if ($remote_height && $remote_width && $remote_depth);

my $isrwps;
my $within;

my %theme;


sub match {
    my ($string, $pattern)=@_;

    $pattern =~ s/\*/.*/g;
    $pattern =~ s/\?/./g;

    return ($string =~ /^$pattern\z/);
}

sub matchdisplaystring {
    my ($string)=@_;
    return ($string =~ /${main_width}x${main_height}x${main_depth}/i) || 
            ($string =~ /r${remote_width}x${remote_height}x${remote_depth}/i);
}

sub mkdirs
{
    my ($themename) = @_;
    mkdir "$rbdir", 0777;
    mkdir "$rbdir/wps", 0777;
    mkdir "$rbdir/themes", 0777;
    mkdir "$rbdir/icons", 0777;
    mkdir "$rbdir/backdrops", 0777;

    if( -d "$rbdir/wps/$themename") {
  #      print STDERR "wpsbuild warning: directory wps/$themename already exists!\n";
    }
    else
    {
       mkdir "$rbdir/wps/$themename", 0777;
    }
}

sub buildcfg {
    my ($themename) = @_;
    my @out;    

    push @out, <<MOO
\#
\# generated by buildtheme.pl
\# $themename is made by $theme{"Author"}
\#
MOO
;
    my %configs = (
                "Name" => "# Theme Name",
                "WPS" => "wps", "RWPS" => "rwps",
                "FMS" => "fms", "RFMS" => "rfms",
                "SBS" => "sbs", "RSBS" => "rsbs",
                "Font" => "font", "Remote Font" => "remote font",
                "Statusbar" => "statusbar", "Remote Statusbar" => "remote statusbar",
                "selector type" => "selector type", "Remote Selector Type" => "remote selector type",  
                "backdrop" => "backdrop", "iconset" => "iconset", "viewers iconset" => "viewers iconset",
                "remote iconset" => "remote iconset", "remote viewers iconset" => "remote viewers iconset",
                "Foreground Color" => "foreground color", "Background Color" => "background color", 
                "backdrop" => "backdrop"
                );
                
    while( my ($k, $v) = each %configs )
    {
        if ($k =~ "Name")
        {
            # do nothing
        }
        elsif ($k =~ /WPS|RWPS|FMS|RFMS|SBS|RSBS/ && exists($theme{$k}))
        {
            push (@out, "$v: $themename.$v\n");
        }
        elsif ($k =~ /backdrop/ )
        {
            if (exists($theme{$k}))
            {
                my $dst = $theme{$k};
                $dst =~ s/(\.[0-9]*x[0-9]*x[0-9]*)//;
                push (@out, "$v: $theme{$k}\n");
            }
            else
            {
                push (@out, "$v: -\n");
            }
        }
        elsif (exists($theme{$k}))
        {
            push (@out, "$v: $theme{$k}\n");
        }
        else
        {
            push (@out, "$v: -\n");
        }
    }

  #  if(-f "$rbdir/themes/$themename.cfg") {
  #      print STDERR "wpsbuild warning: $themename.cfg already exists!\n";
  #  }
  #  else {
        open(CFG, ">$rbdir/themes/$themename.cfg");
        print CFG @out;
        close(CFG);
  #  }
}


sub copythemefont
{
    my ($font) = @_;
    #copy the font specified by the theme

    my $o=$font;
    $o =~ s/\.fnt/\.bdf/;
    mkdir "$rbdir/fonts";
    my $cmd ="$ROOT/tools/convbdf -f -o \"$rbdir/fonts/$font\" \"$ROOT/fonts/$o\" ";
    # print "$cmd\n";
    `$cmd`;
}

sub copyiconset
{
    my ($iconset) = @_;
    #copy the icon specified by the theme

    if ($iconset ne '') {
        $iconset =~ s/.rockbox/$rbdir/;
        $iconset =~ /\/(.*icons\/(.*))/i;
        `cp $ROOT/icons/$2 $1`;
    }
}

sub copybackdrop
{
    my ($backdrop) = @_;
    #copy the backdrop file into the build dir
    if ($backdrop ne '') 
    {
        my $dst = $backdrop;
        $dst =~ s/(\.[0-9]*x[0-9]*x[0-9]*)//;
        my $cmd = "cp $ROOT/$backdrop $rbdir/$dst";
        # print "$cmd\n";
        `$cmd`;
    }
}


sub copyskin
{
    my ($themename, $skin, $ext) = @_;
    # we assume that we copy the WPS files from the same dir the WPSLIST
    # file is located in
    my $dir;
    my @filelist;
    my $src;
    my $dest;
    my $sizestring;
        
    if($wpslist =~ /(.*)WPSFILE/) {
        $dir = $1;
        
        # first try the actual filename given to us
        # then $skin.widthxheightxdepths.ext
        # then $skin.ext
        $src = "${dir}$skin.$ext";
        if ( -e $src )
        {
            if ($skin =~ /\w\.(\d*x\d*x\d*).*/)
            {
                $sizestring = $1;
            }
            my $cmd = "cp $src $rbdir/wps/$themename.$ext";
            `$cmd`;
        }
        else
        {
            my $is_remote = ($ext =~ /^r.../i);
            my $width = $is_remote ? $remote_width : $main_width;
            my $height = $is_remote ? $remote_height : $main_height;
            my $depth = $is_remote ? $remote_depth : $main_depth;
            
            foreach my $d (@depthlist)
            {
                next if ($d > $depth);
                $sizestring = "${width}x${height}x${d}";
                $src = "${dir}$skin.${sizestring}.$ext";
                last if (-e $src);
            }
            if (-e $src)
            {
                my $cmd = "cp $src $rbdir/wps/$themename.$ext";
                `$cmd`;
            }
            elsif (-e "${dir}$skin.$ext")
            {
                my $cmd = "cp ${dir}$skin.$ext $rbdir/wps/$themename.$ext";
                `$cmd`;
            }
            else
            {
                #print STDERR "buildtheme warning: No suitable skin file for $ext\n";
                return;
            }
        }
        
        open(WPSFILE, "$rbdir/wps/$themename.$ext");
        while (<WPSFILE>) {
           $filelist[$#filelist + 1] = $1 if (/\|([^|]*?.bmp)\|/);
        }
        close(WPSFILE);
        if ($#filelist >= 0)
        {
            my $file;
            if ($sizestring && -e "$dir/$themename/$sizestring") 
            {
                foreach $file (@filelist) 
                {
                    system("cp $dir/$themename/$sizestring/$file $rbdir/wps/$themename/");
                }
            }
            elsif (-e "$dir/$themename") 
            {
                foreach $file (@filelist) 
                {
                    system("cp $dir/$themename/$file $rbdir/wps/$themename/");
                }
            }
            else
            {
                print STDERR "beep, no dir to copy WPS from!\n";
            }
        }
    }
}

open(WPS, "<$wpslist");
while(<WPS>) {
    my $l = $_;
    
    # remove CR
    $l =~ s/\r//g;
    if($l =~ /^ *\#/) {
        # skip comment
        next;
    }
    if($l =~ /^ *<(r|)wps>/i) {
        $isrwps = $1;
        $within = 1;
        undef %theme;
        next;
    }
    if($within) {
        if($l =~ /^ *<\/${isrwps}wps>/i) {
            #get the skin directory
            $wpslist =~ /(.*)WPSLIST/;
            my $wpsdir = $1;
            $within = 0;
            
            next if (!exists($theme{Name}));
            mkdirs($theme{Name});
            buildcfg($theme{Name});
            
            copyskin($theme{"Name"}, $theme{"WPS"}, "wps") if exists($theme{"WPS"});
            copyskin($theme{"Name"}, $theme{"RWPS"}, "rwps") if exists($theme{"RWPS"});
            copyskin($theme{"Name"}, $theme{"FMS"}, "fms") if exists($theme{"FMS"});
            copyskin($theme{"Name"}, $theme{"RFMS"}, "rfms") if exists($theme{"RFMS"});
            copyskin($theme{"Name"}, $theme{"SBS"}, "sbs") if exists($theme{"SBS"});
            copyskin($theme{"Name"}, $theme{"RSBS"}, "rsbs") if exists($theme{"RSBS"});
            
            copyiconset($theme{"iconset"}) if exists($theme{"iconset"});
            copyiconset($theme{"remote iconset"}) if exists($theme{"remote iconset"});
            copyiconset($theme{"viewers iconset"}) if exists($theme{"viewers iconset"});
            copyiconset($theme{"remote viewers iconset"}) if exists($theme{"remote viewers iconset"});
            
            copythemefont($theme{"Font"}) if exists($theme{"Font"});
            copythemefont($theme{"Remote Font"}) if exists($theme{"Remote Font"});
            
            copybackdrop($theme{"backdrop"}) if exists($theme{"backdrop"});
            
            
            
        }
        elsif($l =~ /^([\w ]*)\.?(.*):\s*(.*)/) {
            my $var = $1;
            my $extra = $2;
            my $value = $3;
            if (!exists($theme{$var}))
            {
                if (!$extra || 
                    ($extra && (match($target, $extra) || matchdisplaystring($extra))))
                {
                    $theme{$var} = $value;
                    #print "\'$var\': $value\n";
                }
            }
        }
        else{
            #print "Unknown line:  $l!\n";
        }
    }
}

close(WPS);
