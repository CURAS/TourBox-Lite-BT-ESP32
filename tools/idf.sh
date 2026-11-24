#!/bin/zsh
# Optional convenience wrapper for this Mac's ESP-IDF Installation Manager.
set -e
if [[ -z "${IDF_PATH:-}" ]]; then
    source /Users/ramda/.espressif/tools/activate_idf_v6.1.1.sh
fi
export IDF_COMPONENT_CACHE_PATH="${PWD}/tmp/component-cache"
idf.py "$@"
