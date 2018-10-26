#!/usr/bin/env perl

#
# Copyright (C) Valentin V. Bartenev
#

use strict;
use warnings;

use Test::More;
use Test::Nginx;

my $t = Test::Nginx->new()->has(qw/http/)->plan(12);

$t->write_file_expand('nginx.conf', <<'CONF');

%%TEST_GLOBALS%%

master_process off;
daemon         off;

events {
}

http {
	%%TEST_GLOBALS_HTTP%%
	ctpp2 on;
	ctpp2_steps_limit  30;

	server {
		listen       127.0.0.1:8080;
		server_name  localhost;

		templates_root  %%TESTDIR%%;

		location /tmpl_notfound {
			template   nil.ct2;
			try_files  /dummy.json =404;
		}
		location /bad_tmpl {
			template   dummy.tmpl;
			try_files  /dummy.json =404;
		}
		location /bad_tmpl_checksum {
			templates_check on;
			template   broken.ct2;
			try_files  /dummy.json =404;
		}

		location /bad_json {
			template   dummy.ct2;
			try_files  /dummy.tmpl =404;
		}

		location /steps_limit {
			template   loop.ct2;
			try_files  /array.json =404;
		}

		location /wrong_func {
			template   func.ct2;
			try_files  /dummy.json =404;
		}
	}
}

CONF

our $d = $t->testdir();

$t->write_file('dummy.tmpl', 'qwerty' x 10);
system("ctpp2c '$d/dummy.tmpl' '$d/dummy.ct2'") == 0 or die "Can't compile dummy template\n";
system("dd if='$d/dummy.ct2' of='$d/broken.ct2' ibs=1 count=" . ((-s "$d/dummy.ct2") - 3)) == 0
	or die "Can't create broken version of dummy template\n";
$t->write_file('dummy.json', '{}');

$t->write_file('loop.tmpl', '<TMPL_loop array><TMPL_var __COUNTER__><br></TMPL_loop>');
system("ctpp2c '$d/loop.tmpl' '$d/loop.ct2'") == 0 or die "Can't compile array template\n";
$t->write_file('array.json', '{"array":[""' . ',""' x 30 . ']}');

$t->write_file('func.tmpl', '<TMPL_var WRONGFUNCTION()>');
system("ctpp2c '$d/func.tmpl' '$d/func.ct2'") == 0 or die "Can't compile wrong function template\n";

$t->run();

my $e500 = qr{^HTTP/1\.[01] 500}i;

like http_get('/tmpl_notfound'), $e500, 'Template file not found (response)';
ok check_log(qq{Opening template file "$d/nil.ct2" failed}), 'Template file not found (log)';

like http_get('/bad_tmpl'), $e500, 'Not compiled template (response)';
ok check_log(q/CTPP2 template test: it doesn'\''t look like compiled template/), 'Not compiled template (log)';

like http_get('/bad_tmpl_checksum'), $e500, 'Bad template checksum (response)'; 
ok check_log('CTPP2 template test: CRC checksum invalid'), 'Bad template checksum (log)';

like http_get('/bad_json'), $e500, 'Bad JSON (response)';
ok check_log('CTPP generic exception: not an JSON object'), 'Bad JSON (log)';

like http_get('/steps_limit'), $e500, 'Steps limit (response)';
ok check_log('VM error: Execution limit of steps reached at 0x'), 'Steps limit (log)';

like http_get('/wrong_func'), $e500, 'Wrong function call (response)';
ok check_log('VM error: Unsupported syscall "WRONGFUNCTION"'), 'Wrong function call (log)';

sub check_log {
	my $msg = shift;
	my $e = $d . '/error.log';
	return system("grep -qF '$msg' '$e'") == 0;
}
