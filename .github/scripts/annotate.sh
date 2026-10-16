#!/bin/bash
# annotate.sh TITLE FILE EXITCODE
# Shows the first 3500 characters of FILE as a GitHub annotation on the run (a notice when EXITCODE is 0,
# an error otherwise), so the result can be read from the run's page or the API without opening the raw logs.
title="$1"
file="$2"
status="${3:-0}"
msg=$(head -c 3500 "$file" 2>/dev/null | sed 's/%/%25/g' | awk '{printf "%s%%0A", $0}')
if [ "$status" -eq 0 ]; then level=notice; else level=error; fi
echo "::$level title=$title (exit $status)::$msg"
