// This is a edge-case tester to make sure the beginning and end
// of a the BlockFuse'd device is the same as the source device.
//
// To use: 
//   ./test-bytes /dev/sda1 /mnt/block-devices/sda1
//
// This is read-only so it is safe.

#include <stdlib.h>
#include <stdio.h>

#define TEST_BYTES 10

int main(int argc,char **argv)
{
	FILE *in1, *in2;
	int i;
	if (argc < 3)
	{
		printf("usage: last-byte file1 file\n  Prints the bytes that differ at the end of a file.\n");
		exit(1);
	}


	in1 = fopen(argv[1], "r");
	in2 = fopen(argv[2], "r");

	for (i = 0 ; i < TEST_BYTES; i++)
	{
		fseek(in1, i, SEEK_SET);
		fseek(in2, i, SEEK_SET);
		int c1 = fgetc(in1), c2 = fgetc(in2);
		if (c1 != c2 || c1 < 0 || c2 < 0)
			printf("BOF+%d differs: %02X / %02X\n", i, c1, c2);
	}
	printf("\n");
	// This should test EOF properly too, since we start at 0.
	for (i = 0 ; i < TEST_BYTES; i++)
	{
		fseek(in1, -i, SEEK_END);
		fseek(in2, -i, SEEK_END);
		int c1 = fgetc(in1), c2 = fgetc(in2);
		if (c1 != c2)
			printf("EOF-%d differs: %02X / %02X\n", i, c1, c2);
	}

	return 0;
}
