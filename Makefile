all: clean build run_acp

clean:
	rm -f acp

build:
	gcc -o acp acp.c -lrt -laio

run_acp: clean build
	./run_acp.sh

run_cp: clean build
	./run_cp.sh
