#!/usr/bin/env perl

#
# Copyright (C) Valentin V. Bartenev
#

use strict;
use warnings;
use feature ':5.10';

use Test::More;
use Test::Nginx;

my $t = Test::Nginx->new()->has(qw/http proxy/)->plan(7);

$t->write_file_expand('nginx.conf', <<'CONF');

%%TEST_GLOBALS%%

master_process off;
daemon         off;

events {
}

http {
	%%TEST_GLOBALS_HTTP%%
	ctpp2 on;

	server {
		listen       127.0.0.1:8080;
		server_name  localhost;

		templates_root  %%TESTDIR%%;

		location / {
			proxy_pass  http://127.0.0.1:8081;
		}
		location /mod {
			templates_header  x-tmpl;
			proxy_pass  http://127.0.0.1:8081;
		}
		location /pr {
			template    /nil.ct2;
			proxy_pass  http://127.0.0.1:8081;
		}
		location /varroot {
			templates_root  /$document_root;
			proxy_pass  http://127.0.0.1:8081;
		}
		location /rootover {
			templates_root  /nil;
			proxy_pass  http://127.0.0.1:8081;
		}
	}
}

CONF

our $d = $t->testdir();

$t->write_file('hw.tmpl', 'Hello <TMPL_var second>!');
system("ctpp2c '$d/hw.tmpl' '$d/hw.ct2'") == 0 or die "Can't compile 'Hello world' template\n";

$t->run_daemon(\&http_daemon);
$t->run();

my $get = http_get('/');
like $get,    qr/^Hello world!$/m,  'Default header works';
unlike $get,  qr/^X-Template/mi,    'Default header cleared';

$get = http_get('/mod');
like $get,    qr/^Hello wrld!$/m,   'Modified header works';
unlike $get,  qr/^X-Tmpl/mi,        'Modified header cleared';

like http_get('/pr'),        qr/^Hello world!$/m,  'Header domination';
like http_get('/varroot'),   qr/^Hello world!$/m,  'Header and variable root';
like http_get('/rootover'),  qr/^Hello world!$/m,  'Header root override';

sub http_daemon {
	my $server = IO::Socket::INET->new(
		Proto => 'tcp',
		LocalHost => '127.0.0.1:8081',
		Listen => 5,
		ReuseAddr => 1
	) or die "Can't create listening socket: $!\n";

	while (my $client = $server->accept()) {
		my ($r) = <$client> =~ m{^GET (.+) HTTP/1\.[01]\r$};
		while (<$client>) {
			last if /^\r\n$/;
		}

		given($r) {
			when ('/mod') {
				print $client <<'RESP';
HTTP/1.1 200 OK
Connection: close
X-Tmpl: hw.ct2

{"second":"wrld"}
RESP
			}
			when ('/rootover') {
				print $client <<"RESP";
HTTP/1.1 200 OK
Connection: close
X-Template: $d/hw.ct2

{"second":"world"}
RESP
			}
			default {
				print $client <<'RESP';
HTTP/1.1 200 OK
Connection: close
X-Template: hw.ct2

{"second":"world"}
RESP
			}
		}

		close $client;
	}
}
