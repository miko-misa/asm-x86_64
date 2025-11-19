#!/usr/bin/env sh

if [ -z "${TEST_SH_PREFERRED_SHELL:-}" ]; then
    if command -v zsh >/dev/null 2>&1; then
        export TEST_SH_PREFERRED_SHELL=zsh
        exec zsh "$0" "$@"
    elif command -v bash >/dev/null 2>&1; then
        export TEST_SH_PREFERRED_SHELL=bash
        exec bash "$0" "$@"
    else
        echo "Error: this script requires either zsh or bash" >&2
        exit 1
    fi
fi

if [ -n "${ZSH_VERSION:-}" ]; then
    setopt KSH_ARRAYS
    setopt SH_WORD_SPLIT
fi

set -euo pipefail

if [[ $# -lt 2 ]]; then
    echo "Usage: $0 <parser.c> <testcases.txt> [--makefile <path>]" >&2
    exit 1
fi

cli_makefile=""
args=()

while [[ $# -gt 0 ]]; do
	case "$1" in
		--makefile|-m)
			if [[ $# -lt 2 ]]; then
				echo "Missing path after $1" >&2
				exit 1
			fi
			cli_makefile=$2
			shift 2
			;;
		--)
			shift
			while [[ $# -gt 0 ]]; do
				args+=("$1")
				shift
			done
			break
			;;
		*)
			args+=("$1")
			shift
			;;
	esac
done

if (( ${#args[@]} != 2 )); then
    echo "Usage: $0 <parser.c> <testcases.txt> [--makefile <path>]" >&2
    exit 1
fi

parser_src=${args[0]}
testcases_file=${args[1]}

if [[ ! -f $parser_src ]]; then
	echo "Parser source not found: $parser_src" >&2
	exit 1
fi

if [[ ! -f $testcases_file ]]; then
	echo "Testcases file not found: $testcases_file" >&2
	exit 1
fi

parser_path=$(cd -- "$(dirname "$parser_src")" && pwd)/$(basename "$parser_src")
testcases_path=$(cd -- "$(dirname "$testcases_file")" && pwd)/$(basename "$testcases_file")
parser_base=$(basename "$parser_path" .c)

cwd=$(pwd)
output_dir="$cwd/output"
mkdir -p "$output_dir"

parser_bin="$output_dir/${parser_base}_compiler"
asm_tmp="$output_dir/${parser_base}_test_temp.s"
program_tmp="$output_dir/${parser_base}_test_temp_program"

select_makefile() {
	if [[ -n $cli_makefile ]]; then
		printf "%s\n" "$cli_makefile"
		return 0
	fi

	override_tmp=${MAKEFILE_OVERRIDE:-}
	if [[ -n $override_tmp ]]; then
		printf "%s\n" "$override_tmp"
		return 0
	fi

	case "$(uname -s)" in
		Darwin) printf "%s/Makefile.macos\n" "$cwd" ;;
		Linux) printf "%s/Makefile.linux\n" "$cwd" ;;
		*) printf "%s/Makefile.macos\n" "$cwd" ;;
	esac
}

makefile=$(select_makefile)

if [[ ! -f $makefile ]]; then
	echo "Makefile not found: $makefile" >&2
	exit 1
fi

make -s -f "$makefile" parser SRC="$parser_path" BIN="$parser_bin"

cleanup() {
	rm -f "$asm_tmp" "$program_tmp"
}

trap cleanup EXIT

total=0
failed=0

while IFS= read -r raw_line || [[ -n $raw_line ]]; do
	line=$(printf "%s" "$raw_line" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')
	[[ -z $line ]] && continue
	[[ ${line:0:1} == "#" ]] && continue
	if [[ ${line:0:1} == "-" ]]; then
		line=${line#-}
		line=$(printf "%s" "$line" | sed -e 's/^[[:space:]]*//')
	fi
	if [[ $line != *,* ]]; then
		echo "Invalid test case (missing comma): $raw_line" >&2
		exit 1
	fi
	expression=${line%,*}
	expression=$(printf "%s" "$expression" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')
	expected=${line##*,}
	expected=$(printf "%s" "$expected" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')
	[[ -z $expression ]] && continue
	(( ++total ))

	"$parser_bin" "$expression" > "$asm_tmp"

	make -s -f "$makefile" program ASM="$asm_tmp" OUT="$program_tmp"

	set +e
	program_output=$("$program_tmp" 2>&1)
	cmd_status=$?
	set -e

	actual=${program_output%%$'\n'*}
	actual=${actual//$'\r'/}
	actual=$(printf "%s" "$actual" | sed -e 's/[[:space:]]*$//')

	if [[ "$actual" == "$expected" ]]; then
		echo "[$total] PASS: $expression => $actual"
	else
		echo "[$total] FAIL: $expression => expected '$expected' but got '$actual' (exit $cmd_status)"
		(( ++failed ))
	fi
done < "$testcases_path"

if [[ $total -eq 0 ]]; then
	echo "No test cases found in $testcases_path" >&2
	exit 1
fi

if [[ $failed -ne 0 ]]; then
	echo "Summary: $failed / $total test cases failed."
	exit 1
else
	echo "Summary: All $total test cases passed."
fi
