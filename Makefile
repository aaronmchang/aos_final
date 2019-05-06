all: clean build run

clean:
	rm -f acp
	rm -rf copydir

build:
	gcc -o acp acp.c -lrt

run:
	time ./acp sourcedir/ copydir/
