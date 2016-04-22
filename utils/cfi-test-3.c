#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma GCC optimize 0

void yes_function(int param)
{
	printf("yes: %i\n", param);
}

void no_function(int param)
{
	if ( param )
		system("undesirable-command");
	printf("no: %i\n", param);
}

void adversary(void)
{
	uintptr_t array[1];
	for ( size_t i = 0; i < 16; i++ )
		array[i] = (uintptr_t) no_function; /* buffer overrun */
	(void) array;
}

int main(int argc, char* argv[])
{
	int param = 2 <= argc ? atoi(argv[1]) : 1;
	void (*ptr)(int) = no_function;
	if ( param )
		ptr = yes_function;
	adversary();
	ptr(param);
	return 0;
}
