all: block-fuse.c
	gcc -O2 -Wall -D_FILE_OFFSET_BITS=64 block-fuse.c -o block-fuse -lfuse
	gcc -O2 -Wall -D_FILE_OFFSET_BITS=64 test-bytes.c -o test-bytes

