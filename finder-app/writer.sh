#!/bin/bash

if ! [ $# -eq 2 ]
then 
    echo "This script requires 2 arguments"
    exit 1
fi

install -D /dev/null $1

if ! [ -e $1 ]
then 
    echo "File cannot be created"
    exit 1
fi

echo $2 > $1
