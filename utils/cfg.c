#include <sys/stat.h>

#include <err.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

struct control_flow
{
	uintptr_t from;
	uintptr_t to;
};

struct control_flow* cfg = NULL;
size_t cfg_length = 0;

static int cfcmp(const void* a_ptr, const void* b_ptr)
{
	const struct control_flow* a = (const struct control_flow*) a_ptr;
	const struct control_flow* b = (const struct control_flow*) b_ptr;
	if ( b->from < a->from )
		return 1;
	if ( b->from > a->from )
		return -1;
	if ( b->to < a->to )
		return 1;
	if ( b->to > a->to )
		return -1;
	return 0;
}

int main(int argc, char* argv[])
{
	for ( int i = 1; i < argc; i++ )
	{
		FILE* fp = fopen(argv[i], "r");
		if ( !fp )
			err(1, "%s", argv[i]);
		struct stat st;
		if ( fstat(fileno(fp), &st) < 0 )
			err(1, "stat: %s", argv[i]);
		size_t entries = st.st_size / sizeof(struct control_flow);
		size_t new_length = cfg_length + entries;
		size_t new_size = new_length * sizeof(struct control_flow);
		cfg = (struct control_flow*) realloc(cfg, new_size);
		if ( !cfg )
			err(1, "malloc");
		if ( fread(cfg + cfg_length, sizeof(struct control_flow), entries, fp) != entries )
			err(1, "%s", argv[i]);
		cfg_length = new_length;
		fclose(fp);
	}
#if 0
	for ( size_t i = 0; i < cfg_length; i++ )
		fprintf(stderr, "[%zu] 0x%lX -> 0x%lX\n", i, cfg[i].from, cfg[i].to);
#endif
	qsort(cfg, cfg_length, sizeof(struct control_flow), cfcmp);
	size_t new_length = 0;
	for ( size_t i = 0; i < cfg_length; i++ )
	{
		if ( i && cfg[i-1].from == cfg[i].from && cfg[i-1].to == cfg[i].to )
			continue;
		cfg[new_length++] = cfg[i];
	}
	cfg_length = new_length;
	fflush(stdout);
	if ( fwrite(cfg, sizeof(struct control_flow), cfg_length, stdout) != cfg_length )
		err(1, "stdout");
	if ( ferror(stdout) || fflush(stdout) == EOF )
		err(1, "stdout");
#if 1
	for ( size_t i = 0; i < cfg_length; i++ )
		fprintf(stderr, "[%zu] 0x%lX -> 0x%lX\n", i, cfg[i].from, cfg[i].to);
#endif
	return 0;
}
