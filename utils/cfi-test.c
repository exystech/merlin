#include <stdio.h>
#include <string.h>

#pragma GCC optimize 0

void gpf(void)
{
	char buffer[16];
	memset(buffer, 0x11, 128); /* buffer overrun */
}

int main(void)
{
	printf("begun\n");
	gpf();
	printf("done\n");
	return 42;
}
