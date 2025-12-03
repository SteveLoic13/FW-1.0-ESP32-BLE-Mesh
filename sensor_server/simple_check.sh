#!/bin/bash
echo "=== TROVA FUNZIONI NON USATE ==="

# Lista di tutti i file .c
find ./main ./ecolumiere -name "*.c" | while read file; do
    echo "🔍 Analizzo: $file"
    
    # Estrai nomi funzione (pattern semplificato)
    grep -E "^[a-zA-Z_].*[a-zA-Z_].*\(.*\)[[:space:]]*\{?" "$file" | \
    while IFS= read -r line; do
        # Estrai nome funzione (metodo semplice)
        if [[ "$line" =~ ([a-zA-Z_][a-zA-Z_0-9]*)[[:space:]]*\( ]]; then
            func_name="${BASH_REMATCH[1]}"
            
            # Salta parole chiave
            if [[ "$func_name" =~ ^(if|for|while|switch|return)$ ]]; then
                continue
            fi
            
            # Controlla se è usata oltre la definizione
            uses=$(grep -c "\\b$func_name\\s*(" "$file")
            other_uses=$(find ./main ./ecolumiere -name "*.c" -exec grep -c "\\b$func_name\\s*(" {} \; | awk '{sum+=$1} END {print sum}')
            
            if [[ $uses -eq 1 && $other_uses -eq 1 ]]; then
                echo "🚨 POSSIBILE NON USATA: $func_name in $file"
            fi
        fi
    done
done
