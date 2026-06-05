eval '(exit $?0)' && eval 'exec perl -S $0 ${1+"$@"}'
    & eval 'exec perl -S $0 $argv:q'
    if 0;

# -*- perl -*-

my @original_ARGV = @ARGV;

use Env (DDS_ROOT);
use lib "$DDS_ROOT/bin";
use Env (ACE_ROOT);
use lib "$ACE_ROOT/bin";
use PerlDDS::Run_Test;
use strict;

PerlDDS::add_lib_path('../../ConsolidatedMessengerIdl');
PerlDDS::add_lib_path('../../common');

my $status = 0;

my $test = new PerlDDS::TestFramework();

$test->{dcps_debug_level} = 1;
$test->{dcps_transport_debug_level} = 1;
# will manually set -DCPSConfigFile
$test->{add_transport_config} = 0;

# Allow publisher to do asynchronous discovery when reconnecting.
my $async_discovery_opts = " -CertificateIdPattern CN\\s*=\\s*(Tahoe).* -LogAsyncDiscovery 1";

# Require security
my $relay_opts = "-Id relay" .
                 $async_discovery_opts .
                 " -LogDiscovery 1" .
                 " -LogActivity 1" .
                 " -LogRelayStatistics 3" .
                 " -DCPSConfigFile relay1.ini" .
                 " -ApplicationDomain 42" .
                 " -VerticalAddress 4444" .
                 " -HorizontalAddress 127.0.0.1:11444" .
                 " -UserData thisrelay" .
                 " -IdentityCA ../../../security/certs/identity/identity_ca_cert.pem" .
                 " -PermissionsCA ../../../security/certs/permissions/permissions_ca_cert.pem" .
                 " -IdentityCertificate ../../../security/certs/identity/test_participant_01_cert.pem" .
                 " -IdentityKey ../../../security/certs/identity/test_participant_01_private_key.pem" .
                 " -Governance governance_signed.p7s" .
                 " -Permissions permissions_relay_signed.p7s" .
                 " -DCPSSecurity 1";

# Use the same relay for both publisher and subscriber
my $common_ini = " pub_rtps.ini";
my $control_args = "-RelayId relay -DCPSConfigFile relay1.ini -Set RTPS_RELAY_ASYNC_DISCOVERY_CACHE_ONLY=true";

$test->process("relay", "$ENV{DDS_ROOT}/bin/RtpsRelay", $relay_opts);
$test->process("publisher", "publisher", "-ORBDebugLevel 1 -DCPSConfigFile". $common_ini . " -DCPSSecurity 1");
$test->process("publisher2", "publisher", "-ORBDebugLevel 1 -DCPSConfigFile". $common_ini . " -DCPSSecurity 1 -r");
$test->process("subscriber", "subscriber", "-ORBDebugLevel 1 -DCPSConfigFile" . $common_ini . " -DCPSSecurity 1 -r");
$test->process("control", "$ENV{DDS_ROOT}/bin/RtpsRelayControl", "$control_args");

# First, connect the first publisher with the subscriber.
# The relay will build the partition cache for the publisher.
$test->start_process("relay");
sleep 3;
$test->start_process("subscriber");
sleep 3;
$test->start_process("publisher");
sleep 3;
$test->stop_process(5, "publisher");

# Make the relay use only the async discovery cache for routing message from the second publisher.
$test->start_process("control");
sleep 3;
$test->stop_process(5, "control");

# Then, connect the second publisher to the relay.
# The partition cache constructed from the first publisher connection should be used.
$test->start_process("publisher2");
sleep 3;

$test->stop_process(5, "subscriber");
$test->stop_process(5, "publisher2");
$test->kill_process(5, "relay");

exit $test->finish(15);
