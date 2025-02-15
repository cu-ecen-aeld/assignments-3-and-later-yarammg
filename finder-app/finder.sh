#!/bin/bash

if ! [ $# -eq 2 ]
then 
    echo "This script requires 2 arguments"
    exit 1
fi

if ! [ -d $1 ]
then
    echo "${$1} is not a directory"
    exit 1
fi

x=$( tree $1 |  tail -1 | cut -d',' -f 2 | cut -d'f' -f 1 )
y=$( grep -Rl $2 $1 | wc -l )

echo " The number of files are ${x} and the number of matching lines are ${y}."
exit 0

