#!/usr/bin/env perl

#
# Copyright (C) Valentin V. Bartenev
#

use strict;
use warnings; 

use Test::More;
use Test::Nginx;
use File::Basename ('dirname');

BEGIN {
	unless (eval ' use Test::Differences; 1 ') {
		*eq_or_diff = \&is;
	} else { unified_diff(); }
}

my $t = Test::Nginx->new()->has(qw/http/)->plan(9);

my $aio = $t->has_module('--with-file-aio') ? <<'AIO' : '';
location /aio/ {
	sendfile off;
	aio on;
	directio 512;
	template  lebowski-bench-loop.ct2;
	alias %%TESTDIR%%/;
}
AIO

$t->write_file_expand('nginx.conf', <<"CONF");

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
		templates_root %%TESTDIR%%;

		location / {
			template  lebowski-bench-loop.ct2;
		}

		location /proxy/ {
			template  lebowski-bench-loop.ct2;
			proxy_pass http://127.0.0.1:8080/local/;
		}
		location /local/ {
			alias %%TESTDIR%%/;
		}

		location /smallbuf/ {
			output_buffers  1 32;
			template  lebowski-bench-loop.ct2;
			alias %%TESTDIR%%/;
		}
		location /bigbuf/ {
			output_buffers  5 8m;
			template  lebowski-bench-loop.ct2;
			alias %%TESTDIR%%/;
		}
		$aio
	}
}

CONF

my $test_d = $t->testdir();
my $data_d = dirname(__FILE__) . '/data';

system("ctpp2c '$data_d/lebowski-bench-loop.tmpl' '$test_d/lebowski-bench-loop.ct2'") == 0
	or die "Can't compile 'Lebowski bench' template\n";
symlink "$data_d/lebowski-bench.json", "$test_d/lebowski-bench.json";
my $r = `ctpp2vm '$test_d/lebowski-bench-loop.ct2' '$data_d/lebowski-bench.json' 1024`;
$? == 0 or die "Can't process 'Lebowski bench' template\n";
my $l = length($r);

$t->run();

my ($h, $b) = http_sepget('/lebowski-bench.json');
unlike $h, qr/^Accept-Ranges/im, 'Cleared accept-ranges header';
like $h, qr/^Content-Length: $l\r$/im, 'Check content-length header';
eq_or_diff $b, $r, 'Content check';

($h, $b) = http_sepget('/proxy/lebowski-bench.json');
unlike $h, qr/^Accept-Ranges/im, 'Cleared accept-ranges header (proxy)';
like $h, qr/^Content-Length: $l\r$/im, 'Check content-length header (proxy)';
eq_or_diff $b, $r, 'Content check (proxy)';

($h, $b) = http_sepget('/smallbuf/lebowski-bench.json');
eq_or_diff $b, $r, 'Content check (small buffer)';
($h, $b) = http_sepget('/bigbuf/lebowski-bench.json');
eq_or_diff $b, $r, 'Content check (big buffer)';

SKIP: {
	skip 'AIO', 1 unless $aio;
	($h, $b) = http_sepget('/aio/lebowski-bench.json');
	eq_or_diff $b, $r, 'Content check (AIO)';
}

sub http_sepget {
	my %r = http_get(shift) =~ /^(.+?)\r\n\r\n(.*)$/s;
	return %r;
}
