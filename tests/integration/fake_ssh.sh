#!/usr/bin/env bash
set -euo pipefail

while [[ $# -gt 0 ]]; do
  case "$1" in
    -o)
      shift 2
      ;;
    -O)
      exit 0
      ;;
    -*)
      shift
      ;;
    *)
      break
      ;;
  esac
done

[[ $# -ge 1 ]]
shift
if [[ $# -eq 0 || "$1" == "true" ]]; then
  exit 0
fi

/bin/sh -c "$1"
