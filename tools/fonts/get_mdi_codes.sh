#!/bin/bash

# Read HTML content from standard input
html_content=$(cat)

# Use grep to find the line containing 'var icons' and handle multiline JSON
json_line=$(echo "$html_content" | grep -o -E 'var icons = \[[^;]*\];')

# Use sed to extract the JSON part and add quotes around keys
json=$(echo "$json_line" | sed -E 's/^var icons = //' | sed -E 's/;$//' | sed -E 's/([a-zA-Z0-9_]+):/\"\1\":/g')

# Convert the JSON into a format suitable for jq
json=$(echo "$json" | jq '.')

# Function to escape glob patterns for use with jq regex
escape_glob_pattern() {
    echo "$1" | sed -e 's/\*/.*/g'
}

# Collect hex codes for the specified icon name patterns
hex_codes=()
for pattern in "$@"; do
    regex_pattern=$(escape_glob_pattern "$pattern")
    matches=$(echo "$json" | jq -r --arg pattern "$regex_pattern" '.[] | select(.name | test($pattern)) | .hex')
    if [ -n "$matches" ]; then
        while read -r hex_code; do
            hex_codes+=("0x$hex_code")
        done <<< "$matches"
    fi
done

# Output the hex codes as a comma-separated string
IFS=','; echo "${hex_codes[*]}"

