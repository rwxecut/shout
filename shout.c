#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <signal.h>
#include <fts.h>
#include <shout/shout.h>

shout_t *shout;

int
compare(const FTSENT** one, const FTSENT** two)
{
    return (strcmp((*one)->fts_name, (*two)->fts_name));
}

int
shout_playfile(shout_t *shout, char *path)
{
	long read, total, ret;
	char buff[4096];
	FILE *track;

	track = fopen(path, "rb");
	total = 0;
	while (1)
	{
		read = fread(buff, 1, sizeof(buff), track);
		total += read;
		if (read > 0)
		{
			ret = shout_send(shout, buff, read);
			if (ret != SHOUTERR_SUCCESS)
			{
				fclose(track);
				return -1;
			}
		}
		else
		{
			break;
		}
	shout_sync(shout);
	}
	fclose(track);
	return 0;
}

void
die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	if (fmt[0] && fmt[strlen(fmt)-1] == ':')
	{
		fputc(' ', stderr);
		perror(NULL);
	}
	else
	{
		fputc('\n', stderr);
	}

	exit(1);
}

void
sigterm(int sig)
{
	fprintf(stderr, "Exit gracefully\n");
	shout_close(shout);
	shout_shutdown();
	exit(0);
}

int
main(int argc, char *argv[])
{
	char track_path[PATH_MAX];
	char *traverse_path[] = {argv[2], NULL};
	char *dot;
	FTS *file_system = NULL;
	FTSENT *child = NULL;
	FTSENT *parent = NULL;
	struct sigaction sigact;

	if (argc != 3)
		die("usage: shout <mount> <dir>");
	
	sigact.sa_handler = sigterm;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigaction(SIGTERM, &sigact, (struct sigaction *)NULL);
	
	shout_init();
	shout = shout_new();
	shout_set_host(shout, "127.0.0.1");
	shout_set_port(shout, 8000);
	shout_set_protocol(shout, SHOUT_PROTOCOL_HTTP);
	shout_set_user(shout, "source");
	shout_set_password(shout, "hackme");
	shout_set_format(shout, SHOUT_FORMAT_MP3);
	shout_set_mount(shout, argv[1]);

	if (shout_open(shout) != SHOUTERR_SUCCESS)
		die("Failed to connect");

	while(1)
	{
		file_system = fts_open(traverse_path,FTS_COMFOLLOW | FTS_NOCHDIR,&compare);
		if (file_system == NULL) die("No such file or directory");

		while( (parent = fts_read(file_system)) != NULL)
		{
			child = fts_children(file_system,0);
			while ((NULL != child) && (NULL != child->fts_link))
			{
				child = child->fts_link;
				dot = strrchr((child->fts_name), '.');

				if (!(dot && !strcasecmp(dot, ".mp3"))) continue;
				memset(track_path, 0, sizeof(track_path));
				sprintf(track_path,"%s/%s",child->fts_path,child->fts_name);
				shout_playfile(shout,track_path);
			}
		}
		fts_close(file_system);
	}
}