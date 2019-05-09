all: clean build run_lacp

clean:
	rm -f acp
	rm -f acp-readahead
	rm -f acp-fallocate
	rm -f acp-nice
	rm -f lacp
	rm -f lacp-readahead
	rm -f lacp-fallocate
	rm -f lacp-nice
	rm -rf sourcedir/
	rm -rf copydir/

build:
	gcc -o acp acp.c -lrt
	gcc -o acp-readahead acp-readahead.c -lrt
	gcc -o acp-fallocate acp-fallocate.c -lrt
	gcc -o acp-nice acp-nice.c -lrt
	gcc -o lacp lacp.c -laio
	gcc -o lacp-readahead lacp-readahead.c -laio
	gcc -o lacp-fallocate lacp-fallocate.c -laio
	gcc -o lacp-nice lacp-nice.c -laio

run_cp: clean build
	./run_cp.sh

run_acp: clean build
	./run_acp.sh

run_acp_readahead: clean build
	./run_acp_readahead.sh

run_acp_nice: clean build
	./run_acp_nice.sh

run_acp_fallocate: clean build
	./run_acp_fallocate.sh

run_lacp: clean build
	./run_lacp.sh

run_lacp_readahead: clean build
	./run_lacp_readahead.sh

run_lacp_nice: clean build
	./run_lacp_nice.sh

run_lacp_fallocate: clean build
	./run_lacp_fallocate.sh

run_all: clean build run_cp run_acp run_lacp
