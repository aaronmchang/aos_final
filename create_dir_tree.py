#! /usr/bin/python3

import sys
import subprocess
import os

os.makedirs("./sourcedir/level2dir", mode=0o777)

total_size = 1000
number_of_files = 10
if len(sys.argv) > 1:
	total_size = int(sys.argv[1])
if len(sys.argv) > 2:
	number_of_files = int(sys.argv[2])

# Calculate size of files to create
each_file_size = total_size * 1024 * 1024 // number_of_files
each_file_size -= each_file_size % 4096
each_file_size //= 4096
# write files in both dirs
for i in range(number_of_files // 2):
	command = "dd if=/dev/zero of=./sourcedir/file_%d bs=4096 count=%d > /dev/null 2>&1" % (i, each_file_size)
	subprocess.check_output(command, shell=True)
for i in range(number_of_files // 2, number_of_files):
	command = "dd if=/dev/zero of=./sourcedir/level2dir/file_%d bs=4096 count=%d < /dev/null 2>&1" % (i, each_file_size)
	subprocess.check_output(command, shell=True)
