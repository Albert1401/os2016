#!/bin/bash

for element in (find -L $1); do
	if [ -h "$element" -a ! -e "$element" -a `stat --format=%Y $element` -le $(( `date +%s` - 604800 )) ] 
           then echo "$element"
        fi
	done
