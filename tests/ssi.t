#!/usr/bin/env perl

#
# Copyright (C) Valentin V. Bartenev
#

use strict;
use warnings;

use Test::More;
use Test::Nginx;

my $t = Test::Nginx->new()->has(qw/http ssi/)->plan(2);

$t->write_file_expand('nginx.conf', <<'CONF');

%%TEST_GLOBALS%%

master_process off;
daemon         off;

events {
}

http {
	%%TEST_GLOBALS_HTTP%%

	server {
		listen       127.0.0.1:8080;
		server_name  localhost;
		templates_root %%TESTDIR%%;

		location / {
			ctpp2 on;
			template page.ct2;
		}
		location /ssi.html {
			ssi on;
		}
		location /ssi.json {
			default_type text/html;
			ssi on;
			ctpp2 on;
			template page.ct2;
		}
	}
}

CONF

my $d = $t->testdir();
my $ssi = 'P1: <!--# include virtual="/page1.json" --> P2: <!--# include virtual="/page2.json" -->';

$t->write_file('ssi.html', $ssi);

$t->write_file('page1.json', '{"v":"foo"}');
$t->write_file('page2.json', '{"v":"bar"}');
$t->write_file('ssi.json', "{'v':'$ssi'}");

$t->write_file('page.tmpl', '<TMPL_var v>');
system("ctpp2c '$d/page.tmpl' '$d/page.ct2'") == 0 or die "Can't compile page template\n";

$t->run();

like(http_get('/ssi.html'), qr/^P1: foo P2: bar$/m, 'Simple');
like(http_get('/ssi.json'), qr/^P1: foo P2: bar$/m, 'Matryoshka');
