#!/bin/bash

for total_size in 100 500 1000 2000
do
	for num_files in 16 32 64 128
	do
		rm -rf sourcedir/
		rm -rf copydir/
		echo "$total_size MB, $num_files files"
		./create_dir_tree.py $total_size $num_files
		/usr/bin/time -f "%E" cp -r sourcedir/ copydir/
		echo
	done
done
