#!/usr/bin/env sh

if [ -z "${RUN_SH_PREFERRED_SHELL:-}" ]; then
    if command -v zsh >/dev/null 2>&1; then
        export RUN_SH_PREFERRED_SHELL=zsh
        exec zsh "$0" "$@"
    elif command -v bash >/dev/null 2>&1; then
        export RUN_SH_PREFERRED_SHELL=bash
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

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <parser.c> [expression] [--makefile <path>]" >&2
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

if (( ${#args[@]} < 1 )); then
    echo "Usage: $0 <parser.c> [expression] [--makefile <path>]" >&2
    exit 1
fi

src=${args[0]}
if (( ${#args[@]} >= 2 )); then
    expr=${args[1]}
else
	expr="3+4="
fi

if [[ ! -f $src ]]; then
	echo "Source file not found: $src" >&2
	exit 1
fi

# Resolve absolute path for the source file
src_path=$(cd -- "$(dirname "$src")" && pwd)/$(basename "$src")
src_base=$(basename "$src_path" .c)

cwd=$(pwd)
output_dir="$cwd/output"
mkdir -p "$output_dir"

parser_bin="$output_dir/${src_base}_compiler"
asm_file="$output_dir/${src_base}_generated.s"
result_bin="$output_dir/${src_base}_program"

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

make -s -f "$makefile" parser SRC="$src_path" BIN="$parser_bin"

"$parser_bin" "$expr" > "$asm_file"

make -s -f "$makefile" program ASM="$asm_file" OUT="$result_bin"

"$result_bin"
