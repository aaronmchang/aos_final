all: clean build run_lacp

clean:
	rm -f acp
	rm -f lacp

build:
	gcc -o acp acp.c -lrt
	gcc -o lacp lacp.c -laio

run_cp: clean build
	./run_cp.sh

run_acp: clean build
	./run_acp.sh

run_lacp: clean build
	./run_lacp.sh

run_all: run_cp run_acp run_lacp
