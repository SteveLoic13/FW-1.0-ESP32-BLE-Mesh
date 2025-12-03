#!/bin/bash
echo "🔍 Cercando funzioni statiche non usate..."

# Cerca funzioni statiche definite ma non usate
find ./main ./ecolumiere -name "*.c" -exec grep -l "static.*(" {} \; | while read file; do
    echo "Analizzo: $file"
    grep -n "static.*[a-zA-Z_].*(" "$file" | while read line; do
        func=$(echo "$line" | sed 's/.*static.*[a-zA-Z_][a-zA-Z_0-9]* \(\w*\)(.*/\1/')
        if [[ ! -z "$func" ]]; then
            count=$(grep -c "[^a-zA-Z0-9_]$func[^a-zA-Z0-9_]" "$file")
            if [[ $count -eq 1 ]]; then
                echo "🚨 POSSIBILE FUNZIONE NON USATA: $func in $file"
            fi
        fi
    done
done
