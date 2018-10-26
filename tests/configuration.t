#!/usr/bin/env perl

#
# Copyright (C) Valentin V. Bartenev
#

use strict;
use warnings;

use Test::More;
use Test::Nginx;
use File::Path ('mkpath');

my $t = Test::Nginx->new()->has(qw/http/)->plan(8);

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
			template  hw.ct2;
		}
		location /off {
			ctpp2 off;
			template   hw.ct2;
			try_files  /hw.json =404;
		}

		location /cat {
			templates_root  sub1/sub2;
			template        hw_s.ct2;
			try_files       /hw.json =404;
		}
		location /cat2 {
			templates_root  /nil;
			template        %%TESTDIR%%/hw.ct2;
			try_files       /hw.json =404;
		}
		location /cat3 {
			templates_root  %%TESTDIR%%/sub1;
			template        sub2/hw_s.ct2;
			try_files       /hw.json =404;
		}

		location ~ ^/var-(.+)-(.+) {
			templates_root  sub1/$2;
			template   $1_s.${is_args}ct2;
			try_files  /hw.json =404;
		}

		location /check {
			templates_check on;
			template   hw.ct2;
			try_files  /hw.json =404;
		}

		location /cached {
			template   cached hw.ct2;
			try_files  /hw.json =404;
		}
	}
}

CONF

my $d = $t->testdir();

$t->write_file('hw.tmpl', 'Hello <TMPL_var second>!');
system("ctpp2c '$d/hw.tmpl' '$d/hw.ct2'") == 0 or die "Can't compile 'Hello world' template\n";
$t->write_file('hw.json', '{"second":"world"}');
mkpath "$d/sub1/sub2";
symlink "$d/hw.ct2", "$d/sub1/sub2/hw_s.ct2";

$t->run();

like http_get('/hw.json'),  qr/^Hello world!$/m,        'Hello world';
like http_get('/off'),      qr/^{"second":"world"}$/m,  'ctpp2 off';

like http_get('/cat'),   qr/^Hello world!$/m,  'Path concatination';
like http_get('/cat2'),  qr/^Hello world!$/m,  'Path concatination 2';
like http_get('/cat3'),  qr/^Hello world!$/m,  'Path concatination 3';

like http_get('/var-hw-sub2'),  qr/^Hello world!$/m,  'Variables in "templates_root" and "template"';

like http_get('/check'),   qr/^Hello world!$/m,  'Template check ok';
like http_get('/cached'),  qr/^Hello world!$/m,  'Cached template';