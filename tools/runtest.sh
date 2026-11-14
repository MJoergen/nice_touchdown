#!/bin/sh
# runtest.sh NAME CHECK INPUT host|nice|screen PROGRAM [ARGS...]
#
# Runs PROGRAM with INPUT as its console input, on the host or in
# niceemu ($NICEEMU, with --step-time, so that its clock counts steps),
# and leaves its output in build/test/NAME.raw. screen is nice with
# --color, so that the screen at the halt, with its colors, is in the
# output too.
#
# The lines that match a pattern of tests/CHECK.grep (with what
# follows the first digit or _ of CHECK left out: perftsuite5 uses
# perftsuite.grep, and screen_game screen.grep), after the
# prompts "> " are taken off, and then "exit N" with main's result,
# with the figures of the players' clocks on the screen as m:ss, and
# the version that follows "nice_touchdown " ($VERSION, which the
# Makefile exports) as VERSION, must be tests/CHECK.expected. Prints
# PASS or FAIL, and exits with 1 on a failure.
set -u
name=$1 check=$2 input=$3 kind=$4 prog=$5
shift 5
dir=build/test
mkdir -p $dir
raw=$dir/$name.raw
out=$dir/$name.out

if [ "$kind" = nice ] || [ "$kind" = screen ]; then
    color=
    [ "$kind" = screen ] && color=--color
    "$NICEEMU" --step-time $color -i "$input" "$prog" < /dev/null > $raw
    r1=$(grep -o 'R1  [0-9a-f]*' $raw | cut -c5-)
    if grep -q 'Halted (HALT)' $raw && [ -n "$r1" ]; then
        status=$(( 0x$r1 ))
    else
        status="none (the program did not halt)"
    fi
else
    "$prog" "$@" < "$input" > $raw
    status=$?
fi

expected=tests/$check.expected
clocks='/m\(White\|Black\)  /s/[0-9][0-9:]*:[0-9][0-9]/m:ss/'
version="s/nice_touchdown ${VERSION:?the Makefile exports VERSION}/nice_touchdown VERSION/g"
{ sed 's/^\(> \)*//' $raw | grep -f tests/${check%%[_0-9]*}.grep | sed "$clocks; $version"; echo "exit $status"; } > $out
if cmp -s $out $expected; then
    echo "PASS $name"
else
    echo "FAIL $name"
    diff $expected $out
    exit 1
fi
