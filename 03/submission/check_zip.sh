#!/bin/bash

function check_files {
    for i do
        if [ ! -f $i ]; then
            return 1
        fi
    done
}

function check_compile {
    x=1
    cd $1
        make &> /dev/null
        x=$?
    cd ../
    [[ $x -ne 0 ]]
}

function transfer_files {
    files=(
        "Makefile"
        "$2.c"
    )

    if [[ $2 == @('ex1'|'ex2'|'ex3') ]]
    then
        files+=("packer.h")
    fi

    for file in "${files[@]}"; do
        cp $1/$2/$file $2
    done
}

echo "Checking zip file...."

if [ "$#" -ne 1 ]; then
    echo "You should provide your zip file as a parameter and nothing else!"
    exit 1
fi

if ! [[ $1 =~ ^E[0-9]{7}.zip$ ]]; then
    echo "zip file is wrongly named: it should be <NUSNET ID>.zip"
    exit 1
fi

echo "Unzipping file: $1"
if [ ! -f $1 ]; then
    echo "File $1 does not exist."
    exit 1
fi

tmp_folder="test_grading_aeN332Hp"
rm -rf $tmp_folder
mkdir $tmp_folder

cp $1 $tmp_folder

cd $tmp_folder
unzip $1 > /dev/null

echo "Transferring necessary skeleton files"
sk="skeleton"
mkdir $sk
cd $sk
wget "https://www.comp.nus.edu.sg/~ccris/cs2106_ay2122s1/lab3.tar.gz" > /dev/null
tar -zxvf lab3.tar.gz > /dev/null
cd ../

exercises=(ex1 ex2 ex3 ex4 ex5 ex6)
ex1=("ex1/packer.c")
ex2=("ex2/packer.c")
ex2=("ex3/packer.c")
ex4=("ex4/restaurant.h" "ex4/restaurant.c")
ex5=("ex5/restaurant.h" "ex5/restaurant.c")
ex6=("ex6/restaurant.h" "ex6/restaurant.c")

for ex in "${exercises[@]}"; do
    declare -n files=$ex
    if [ ! -d $ex ]
    then
        echo "$ex: Failed - $ex folder missing"
        continue
    fi

    if ! check_files "${files[@]}"
    then
        echo "$ex: Failed - files missing"
        continue
    fi

    transfer_files $sk $ex

    if check_compile $ex
    then
        echo "$ex: Failed - does not compile"
        continue
    fi

    echo "$ex: Success"
done

cd ../
rm -rf $tmp_folder
