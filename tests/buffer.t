#!/usr/bin/env perl

#
# Copyright (C) Valentin V. Bartenev
#

use strict;
use warnings;
use feature ':5.10';

use Test::More;
use Test::Nginx;

use constant CONTENT => 'x' x 1024;

my $t = Test::Nginx->new()->has(qw/http proxy/)->plan(14);

$t->write_file_expand('nginx.conf', <<'CONF');

%%TEST_GLOBALS%%

master_process off;
daemon         off;

events {
}

http {
	%%TEST_GLOBALS_HTTP%%
	ctpp2 on;
	template  %%TESTDIR%%/test.ct2;

	server {
		listen       127.0.0.1:8080;
		server_name  localhost;

		location / {
			proxy_pass  http://127.0.0.1:8081;
		}
		location /conf {
			ctpp2_data_buffer  1k;
			proxy_pass  http://127.0.0.1:8081;
		}
		location /empty.json {}
	}
}

CONF

our $d = $t->testdir();

$t->write_file('test.tmpl', '<TMPL_var t>');
system("ctpp2c '$d/test.tmpl' '$d/test.ct2'") == 0 or die "Can't compile test template\n";

$t->write_file('empty.json', '{}');

$t->run_daemon(\&http_daemon);
$t->run();

my $e500 = qr{^HTTP/1\.[01] 500}i;
my $ok200 = qr{^HTTP/1\.[01] 200}i;

my $r = http_get('/');
like $r, $ok200, 'Ok: H!/C! (header)';
is get_content($r), CONTENT, 'Ok: H!/C! (content)';

$r = http_get('/exact');
like $r, $ok200, 'Ok: H=/C! (header)';
is get_content($r), CONTENT, 'Ok: H=/C! (content)';

$r = http_get('/more');
like $r, $ok200, 'Ok: H>/C! (header)';
is get_content($r), CONTENT, 'Ok: H>/C! (content)';

$r = http_get('/conf_exact');
like $r, $ok200, 'Ok: H=/C< (header)';
is get_content($r), CONTENT, 'Ok: H=/C< (content)';

like http_get('/less'), $e500, 'Error: H</C! (header)';
ok check_log(1), 'Error: H</C! (log)';

like http_get('/conf'), $e500, 'Error: H!/C< (header)';
ok check_log(2), 'Error: H!/C< (log)';

$r = http_get('/empty.json');
like  $r, $ok200, 'Empty output (response)';
like  $r, qr/^Content-Length: 0\r$/im, 'Empty output (content-lenght)';

sub get_content {
	my ($c) = shift =~ /^.+?\r\n\r\n(.*)$/s;
	return $c;
}

sub check_log {
	my $e = $d . '/error.log';
	return `grep -cF 'Data buffer overflow.' '$e'` == shift;
}

sub http_daemon {
	my $server = IO::Socket::INET->new(
		Proto => 'tcp',
		LocalHost => '127.0.0.1:8081',
		Listen => 5,
		ReuseAddr => 1
	) or die "Can't create listening socket: $!\n";

	my $content = '{"t":"' . CONTENT . '"}';
	my $length = length($content);

	while (my $client = $server->accept()) {
		my ($r) = <$client> =~ m{^GET (.+) HTTP/1\.[01]\r$};
		while (<$client>) {
			last if /^\r\n$/;
		}

		print $client "HTTP/1.1 200 OK\r\n";
		given($r) {
			when (/more$/) {
				print $client 'Content-Length: ' . ($length << 1) . "\r\n";
			}
			when (/exact$/) {
				print $client 'Content-Length: ' . $length . "\r\n";
			}
			when (/less$/) {
				print $client 'Content-Length: ' . ($length >> 1) . "\r\n";
			}
		}
		print $client "Connection: close\r\n\r\n$content";

		close $client;
	}
}
