#!/usr/bin/env perl
use strict;
use Data::Dumper;
use CA;
use Test::More ;

# Record tests 
# **************
#
# Test case      | OPCUA-Var.  |RTYP           | write record     | read record     | write record to be read back
# ---------------+-------------+---------------+------------------+-----------------+---------------------------
#  long          |  tstInt     | longin, -out  | REC:setLong      | REC:rdLong      | REC:setRdLong
#  double        |  tstDbl     | ai,ao         | REC:setAo        | REC:rdAo        | REC:setRdAo
#  bit/bool      |  tstBool    | bi,bo         | REC:setBit       | REC:rdBit       | REC:setRdBit
#  bit/long      |  tstInt     | mbbi,mbbo     | REC:setLBit      | REC:rdLBit      | REC:setRdLBit
#  string        |  tstStr     | stringin, -out| REC:setStr       | REC:rdStr       | REC:setRdStr
# slope conv.    |  tstDbl     | ai,ao         | REC:setSlope     | REC:rdSlope     | REC:setRdSlope
# multibit       |  tstInt     | mbbi,mbbo     | REC:setMbb       | REC:rdMbb       | REC:setRdMbb
# multibitDirdct |  tstInt     | mbb[i/o]Direct| REC:setMbbDirect | REC:rdMbbDirect | REC:setRdMbbDirect
# read array     | MyArrayVar  | waveform      | -                | REC:rdIntArr    | -
                                 
my @Pvs = qw(REC:setRdBit REC:setBit REC:rdBit REC:setLBit REC:rdLBit REC:setRdLBit REC:setRdLong REC:setLong REC:rdLong REC:setAo REC:setRdAo REC:rdAo REC:setSlope REC:setRdSlope REC:rdSlope REC:setMbb REC:setRdMbb REC:rdMbb REC:setRdMbbDirect REC:setMbbDirect REC:rdMbbDirect REC:setStr REC:setRdStr REC:rdStr REC:rdIntArr);
my %Chan;
my %Values;     # caget or mon_callback set its values here!
my %monitors;   # for pvConnectMonitor
my $fmt = "%-30s %s\n";
my $pending = 0;   # global count of callbacks, get_callback or put_callback occured
my $ret;        # temporary store return value that show the result of a function
my $pidIoc;
my $pidServer;

$SIG{INT} = sub { 
        killPid($pidIoc) if $pidIoc;
        killPid($pidServer) if $pidServer;
        die "BREAK: '$!'" ;
    };

$pidIoc = startIoc();
warn "**** IOC STARTED";
ok(pvConnect(@Pvs), "PV connect");
warn "**** PVs CONNECTED";

ok(caget("REC:rdLong.UDF"),"Check REC:rdLong.UDF");
#$pidServer = startServer();
warn "START SERVER";
while( $Values{'REC:rdLong.UDF'}->{'VALUE'} eq 'UDF' ) {
    caget("REC:rdLong.UDF");
    print "UDF:",$Values{'REC:rdLong.UDF'}->{'VALUE'},"\n";
}
warn "IOC CONNECTED to SERVER";
# reset all used variables on the server
# Array is incremented by the server
ok(caput("REC:setLong",0), "Set REC:setLong 0");
ok(caput("REC:setAo.OROC",0), "Set REC:setAo.OROC 0");
ok(caput("REC:rdAo.SMOO",0), "Set REC:rdAo.SMOO 0");
ok(caput("REC:setAo",0), "Set REC:setAo 0");
ok(caput("REC:setBit",0), "Set REC:setBit 0");
ok(caput("REC:setStr",'init'), "Set REC:setStr 'init'");

# read used variables on the server - should be 0
ok(caget(qw(REC:rdLong REC:rdAo REC:rdBit REC:rdStr REC:rdSlope)),
        "caget(REC:rdLong REC:rdAo REC:rdBit REC:rdSlope");
ok(cmpVal("REC:rdLong",0), "Can't reset 'REC:rdLong' on the server");
ok(cmpVal("REC:rdAo",0), "Can't reset 'REC:rdAo' on the server");
ok(cmpVal("REC:rdBit",'zero'), "Can't reset 'REC:rdBit' on the server");
ok(cmpVal("REC:rdStr",'init'), "Can't reset 'REC:rdStr' on the server");

# set tstInt variable on the server to perform all binary tests
ok(caput("REC:setLong",3328), "set REC:setLong 3328");

# readback longin, longout and setRdLBit.RVAL should be 3328
ok(caget(qw(REC:setRdLong REC:setLong REC:rdLong REC:setRdLBit.RVAL)),
        "get(REC:setRdLong REC:setLong REC:rdLong REC:setRdLBit.RVAL)");
ok(cmpVal("REC:rdLong",3328), "is REC:rdLong 3328");
ok(cmpVal("REC:setRdLong",3328), "is REC:setRdLong 3328");
ok(cmpVal("REC:setLong",3328), "is REC:setLong 3328");
ok(cmpVal("REC:setRdLBit.RVAL",3328), "is REC:setRdLBit.RVAL 3328");

# readback 3328>>8 = 13 = RVAL. mbbo: 13 = state 3, mbbi 13 = state 13 
ok(caget(qw(REC:setMbb REC:rdMbb REC:setRdMbb REC:setMbbDirect REC:rdMbbDirect REC:setRdMbbDirect)),
        "get(REC:setMbb REC:rdMbb REC:setRdMbb REC:setMbbDirect REC:rdMbbDirect REC:setRdMbbDirect)");
#warn join('\n',map{ "$_=".$Values{$_}->{'VALUE'}} (qw(REC:setMbb REC:rdMbb REC:setRdMbb REC:setMbbDirect REC:rdMbbDirect REC:setRdMbbDirect));
ok(cmpVal("REC:setMbb","three"),"REC:setMbb.VAL three");
ok(cmpVal("REC:rdMbb","thirteen"),"REC:rdMbb.VAL thirteen");
ok(cmpVal("REC:setRdMbb","three"),"REC:setRdMbb.VAL three" );

# readback mbb_Direct: 3328>>8 = 13
ok(cmpVal("REC:setMbbDirect",13),"REC:setMbbDirect.VAL 13");
ok(cmpVal("REC:rdMbbDirect",13),"REC:rdMbbDirect.VAL 13");
ok(cmpVal("REC:setRdMbbDirect",13),"REC:setRdMbbDirect.VAL 13");

# get RVALs - all are 3328
ok(caget(qw(REC:setMbb.RVAL REC:rdMbb.RVAL REC:setRdMbb.RVAL REC:setMbbDirect.RVAL REC:rdMbbDirect.RVAL REC:setRdMbbDirect.RVAL)),
        "get(REC:setMbb.RVAL REC:rdMbb.RVAL REC:setRdMbb.RVAL REC:setMbbDirect.RVAL REC:rdMbbDirect.RVAL REC:setRdMbbDirect.RVAL)");
ok(cmpVal("REC:setMbb.RVAL","3328"),"REC:setMbb.RVAL 3328");
ok(cmpVal("REC:rdMbb.RVAL","3328"),"REC:rdMbb.RVAL 3328");
ok(cmpVal("REC:setRdMbb.RVAL","3328"),"REC:setRdMbb.RVAL 3328" );
ok(cmpVal("REC:setMbbDirect.RVAL","3328"),"REC:setMbbDirect.RVAL 3283");
ok(cmpVal("REC:rdMbbDirect.RVAL","3328"),"REC:rdMbbDirect.RVAL 3328");
ok(cmpVal("REC:setRdMbbDirect.RVAL","3328"),"REC:setRdMbbDirect.RVAL 3328");

# set tstDbl variable on the server to perform all analog tests
ok(caput("REC:setAo","12.345"), "set REC:setAo 12.345");

ok(caget(qw(REC:rdSlope REC:setSlope REC:setRdSlope REC:rdSlope.RVAL REC:setSlope.RVAL REC:setRdSlope.RVAL REC:rdAo.RVAL REC:setAo.RVAL REC:setRdAo.RVAL  REC:rdAo REC:setAo REC:setRdAo)),
        "get all analog records");
#warn "$_: ".$Values{$_}->{'VALUE'} for (qw(REC:rdSlope REC:setSlope REC:setRdSlope REC:rdSlope.RVAL REC:setSlope.RVAL REC:setRdSlope.RVAL REC:rdAo.RVAL REC:setAo.RVAL REC:setRdAo.RVAL  REC:rdAo REC:setAo REC:setRdAo));
ok(cmpVal("REC:rdAo","12.345"),"REC:rdAo 12.345");
ok(cmpVal("REC:rdAo.RVAL","12"),"REC:rdAo.RVAL 12");
ok(cmpVal("REC:rdSlope","143.45"),"REC:rdSlope 143.45");
ok(cmpVal("REC:setAo","12.345"),"REC:setAo 12.345");
ok(cmpVal("REC:setAo.RVAL","12"),"REC:setAo.RVAL 12");
ok(cmpVal("REC:setRdAo","12.345"),"REC:setRdAo 12.345");
ok(cmpVal("REC:setRdAo.RVAL","12"),"REC:setRdAo.RVAL 12");  
ok(cmpVal("REC:setRdSlope","143.45"),"REC:setRdSlope 143.45");
ok(cmpVal("REC:setRdSlope.RVAL","12"),"REC:setRdSlope.RVAL 12");  
ok(cmpVal("REC:setSlope","143.45"),"REC:setSlope 143.45");
ok(cmpVal("REC:setSlope.RVAL","12"),"REC:setSlope.RVAL 12");  

# test OROC and SMOO
ok(caput("REC:setAo","0"), "set REC:setAo 0");
ok(caput("REC:setAo.OROC","0.5"), "set REC:setAo.OROC 0.5");
ok(caput("REC:setAo","5"), "set REC:setAo 5");
ok(caput("REC:rdAo.SMOO","0.9"), "set REC:rdAo.SMOO 0.9");
for (1..10) {
    caput("REC:setAo.PROC","1");
    sleep(1);
}
ok(caget(qw(REC:rdAo REC:setRdAo)));
ok(cmpVal("REC:setRdAo","5"),"set REC:setAo.OROC=0.5 VAL=5, Didn't reach value after 10 times processed: REC:setRdAo=5");
ok(cmpVal("REC:rdAo","2.51905298045"),"REC:rdAo SMOO=0.9 Test, Didn't reach smoothed value after 10 times processed: REC:rdAo=2.51905298045");
ok(caput("REC:setAo.OROC","0"), "set REC:setAo.OROC 0");
warn "\n****************************** TESTING DONE ******************************\n";
done_testing();
sleep(1);
warn "************************** KILL IOC and SERVER ***************************\n";
killPid($pidIoc) if $pidIoc;
killPid($pidServer) if $pidServer;

sub startIoc {
    my $pId = fork();
    die "fork failed: $!" unless defined($pId);
    if (!$pId) {
      exec("./TESTIOC", "../st.cmd.TESTIOC");
      die "exec failed: $!";
    }
    sleep(1);
    return $pId;
}
sub startServer {
    my $pId = fork();
    die "fork failed: $!" unless defined($pId);
    if (!$pId) {
      exec("./testServer","-q");
      die "exec failed: $!";
    }
    sleep(2);
    return $pId;
}

sub killPid {
my ($pId) = @_;    
    CA->pend_event(1);
    kill 9, $pId or die "kill failed: $!";
}

# Just connect all records OR die!
sub pvConnect {
    map { $Chan{$_} = CA->new($_); } @_;
    eval { CA->pend_io(2); };
    if ($@) {
        if ($@ =~ m/^ECA_TIMEOUT/) {
            my @notCon = grep { $Chan{$_}->name if (!$Chan{$_}->is_connected)} keys(%Chan);
            warn "ERROR pvConnect: $@Channels not connected: ", join(' ',@notCon),"\n";
            return 0;
        }
    }
    return 1;
}

# Connect and setup monitors  OR die! Derived from camonitor.pl
sub pvConnectMonitor {
    $pending = scalar(@_);
    map { $Chan{$_} = CA->new($_, \&conn_callback); } @_;
    CA->pend_event(1);
    if ($pending != 0) {
        my @notCon = grep { $Chan{$_}->name if (!$Chan{$_}->is_connected)} keys(%Chan);
        warn "ERROR pvConnectMonitor: $@Channels not connected: ", join(' ',@notCon),"\n";
        die;
    }
}

sub conn_callback {
    my ($chan, $up) = @_;
    $pending--;
    if ($up && ! $monitors{$chan}) {
        my $type = $chan->field_type;
        $type =~ s/^DBR_/DBR_TIME_/;
        $monitors{$chan} = $chan->create_subscription('va', \&mon_callback, $type,0);
    }
}
sub mon_callback {
    my ($chan, $status, $data) = @_;
    my $pv = $chan->name;
    $Values{$pv}->{'MONITOR'} += 1;
    $Values{$pv}->{'STATUS'} = $status;
    $Values{$pv}->{'VALUE'} = $data->{value};
    $Values{$pv}->{'SEVERITY'} = $data->{severity};
    $Values{$pv}->{'TIMESTAMP'} = $data->{stamp};
    $Values{$pv}->{'STAMP_FRACTION'} = $data->{stamp_fraction};
    warn "\t",Dumper($data);
}

# derived from caget.pl
# supose conneced Channels %Chan
sub caget {
    my $ret=1;
    #warn "\tcaget ENTER: ".join(' ',@_);
    
    map{
        my $chan = $Chan{$_};
        unless($chan) {
            pvConnect($_);
            $chan = $Chan{$_};
        }
        if( !$chan or !$chan->is_connected) {
            warn "ERROR caget: '$_' not connected";
            $ret = 0;
        }
        elsif( !$chan->read_access) {
                warn "ERROR caget: Read access denied for '$_'.\n";
                $ret = 0;
        }
        else {
            my $type = $chan->field_type;
            $type =~ s/^DBR_/DBR_TIME_/;
            $chan->get_callback(\&get_callback, $type, 0);
            $pending++;
        }
    } @_; 
    if(($ret == 1) and ($pending > 0)){
        CA->pend_event(0.1) while $pending;
    }
    #warn"\tcaget DONE $ret";
    return $ret;
}

sub get_callback {
    my ($chan, $status, $data) = @_;
    if( $status) {
        warn "\tcaget ",$chan->name," status:",$status,"\n";
    }
    else {
        my $pv = $chan->name;
        $Values{$pv}->{'MONITOR'} += 1;
        $Values{$pv}->{'STATUS'} = $status;
        $Values{$pv}->{'VALUE'} = $data->{value};
        $Values{$pv}->{'SEVERITY'} = $data->{severity};
        my $stamp;
        if (exists $data->{stamp}) {
            my @t = localtime $data->{stamp};
            splice @t, 6;
            $t[5] += 1900;
            $t[0] += $data->{stamp_fraction};
            $stamp = sprintf "%4d-%02d-%02d %02d:%02d:%09.6f", reverse @t;
        }
        $Values{$pv}->{'TIMESTAMP'} = $stamp;
        #warn "\tcaget $pv $Values{$pv}->{'VALUE'} $Values{$pv}->{'STATUS'}";
    }    
    $pending--;
}

# derived from caput.pl -c
sub caput{
    my $pv = shift;
    my @vals = @_;
    my $ret = 1;

    #warn "\tcaput $pv ENTER";
    my $chan = $Chan{$pv};
    unless($chan) {
        print "caput conn:$pv\n";
        pvConnect($pv);
        $chan = $Chan{$pv};
    }
    if( !$chan or !$chan->is_connected) {
        warn "ERROR caput: No pv '$pv' connected";
        $ret = 0;
    }
    elsif( !$chan->write_access) {
            warn "ERROR caput: Write access denied for '$pv'.\n";
            $ret = 0;
    }
    else {
        my $n = $chan->element_count();
        if($n > scalar(@vals)) {
            warn "ERROR caput: Too many values given for '$pv' limit is $n\n";
            $ret = 0;
        }
        else {
            my @values;
            if ($chan->field_type !~ m/ ^ DBR_ (STRING | ENUM | CHAR) $ /x) {
                # Make @values strings numeric
                @values = map { +$_; } @vals;
            } else {
                # Use strings
                @values = @vals;
            }

            $chan->put_callback(\&put_callback, @values);
            CA->pend_event(0.1) until $pending;
            $pending = undef;
            $ret = 1;
        } 
    } 
    CA->pend_event(0.2);    # just wait
    #warn "\tcaput $pv: DONE";
    return $ret;
}
sub put_callback {
    my ($chan, $status) = @_;
    if( $status ) {
        warn "ERROR put_callback: $chan->name: put_callback error: '$status'\n" ;
        $pending = 1;
        return;
    }
    $chan->get_callback(\&new_callback, $chan->field_type);
}

sub new_callback {
    my ($chan, $status, $data) = @_;
    if( $status ) {
        warn "$chan->name: new_callback error: '$status'\n" ;
        return;
    }
    $pending = 1;
}
sub displayData {
    my ($chan, $data) = @_;
    die "Internal error"
        unless ref $data eq 'HASH';
    my $type = $data->{TYPE};
    my $value = $data->{value};
    if (ref $value eq 'ARRAY') {
        $value = join(' ', $data->{COUNT}, @{$value});
    } 
    my $stamp;
    if (exists $data->{stamp}) {
        my @t = localtime $data->{stamp};
        splice @t, 6;
        $t[5] += 1900;
        $t[0] += $data->{stamp_fraction};
        $stamp = sprintf "%4d-%02d-%02d %02d:%02d:%09.6f", reverse @t;
    }
    printf $fmt, $chan->name, join(' ',$stamp, $value, $data->{status}, $data->{severity});
}


# 
sub cmpVal { my ($pv,$val)= @_;
    #warn "\tcmpVal: $pv: '".$Values{$pv}->{'VALUE'}."' eq '$val' is: ",( $Values{$pv}->{'VALUE'} eq $val);
    return ( $Values{$pv}->{'VALUE'} eq $val);
}


