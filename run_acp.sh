#!/bin/bash

for total_size in 100 500 1000 2000
do
	for num_files in 16 32 64 128
	do
		rm -rf sourcedir
		rm -rf copydir
		./create_dir_tree.py $total_size $num_files
		echo "$total_size MB, $num_files files"
		/usr/bin/time -f "%E" ./acp sourcedir/ copydir/
	done
done
