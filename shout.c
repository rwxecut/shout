/* gcc shout.c -o shout -lavutils 
-lavformat -lshout */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <fts.h>
#include <shout/shout.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>

#define HOST "127.0.0.1"
#define PORT 8080

#define USER "source"
#define PASS "hackme"

/* Could be SHOUT_FORMAT_VORBIS or 
SHOUT_FORMAT_MP3 */
#define FORMAT SHOUT_FORMAT_MP3
/* Could be .mp3 or .ogg */
#define EXT ".mp3"

shout_t *shout;
shout_metadata_t *meta;

int
compare(const FTSENT** one, const FTSENT** two)
{
    return (strcmp((*one)->fts_name, (*two)->fts_name));
}

int
extract_meta(shout_metadata_t *meta, char *path)
{
	int ret;
	AVFormatContext *fmt_ctx = NULL;
	AVDictionaryEntry *tag = NULL;

	av_register_all();
	av_log_set_level(AV_LOG_QUIET);

	if((ret = avformat_open_input(&fmt_ctx, path, NULL, NULL)))
		return ret;

	while ((tag = av_dict_get(fmt_ctx->metadata, "", tag, AV_DICT_IGNORE_SUFFIX)))
		shout_metadata_add(meta,tag->key,tag->value);

	avformat_close_input(&fmt_ctx);
	return 0;
}

int
play_file(shout_t *shout, char *path)
{
	int ret;
	long r;
	unsigned char buf[4096];
	FILE *track;

	track = fopen(path, "rb");
	while (r = fread(buf, 1, sizeof(buf), track))
	{
		if ((ret = shout_send(shout, buf, r)) != SHOUTERR_SUCCESS)
			break;
		shout_sync(shout);
	}
	fclose(track);
	return ret;
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
		fputc('\n', stderr);

	exit(1);
}
void
reconnect(shout_t *shout)
{
	int i;

	shout_close(shout);

	fprintf(stderr,"Disconnected, trying to reconnect...\n");

	for(i=0; i<3; i++)
	{
		if(shout_open(shout) == SHOUTERR_SUCCESS) return;
		sleep(2);
	}

	die("Connection failed after 3 retries");
}

void
sigterm(int sig)
{
	fprintf(stderr, "Exit gracefully\n");
	shout_metadata_free(meta);
	shout_close(shout);
	shout_shutdown();
	exit(0);
}

int
main(int argc, char *argv[])
{
	int shouterr;
	char track_path[PATH_MAX];
	char *traverse_path[] = {argv[2], NULL};
	char *dot;
	FTS *file_system = NULL;
	FTSENT *child = NULL;
	FTSENT *parent = NULL;
	struct sigaction sigact;

	if (argc != 3)
		die("usage: shout <mount> <dir>");

	/* set signal handlers */
	sigact.sa_handler = sigterm;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigaction(SIGTERM, &sigact, (struct sigaction *)NULL);
	sigaction(SIGINT, &sigact, (struct sigaction *)NULL);

	/* initialize shout */
	shout_init();
	shout = shout_new();
	meta = shout_metadata_new();
	shout_set_host(shout, HOST);
	shout_set_port(shout, PORT);
	shout_set_protocol(shout, SHOUT_PROTOCOL_HTTP);
	shout_set_user(shout, USER);
	shout_set_password(shout, PASS);
	shout_set_format(shout, FORMAT);
	shout_set_mount(shout, argv[1]);

	if (shout_open(shout) != SHOUTERR_SUCCESS)
		die("Failed to connect");

	/* enter main loop */
	while(1)
	{

		/* if parent NULL reopen 
tree or die */
		if (parent == NULL)
		{
			if (access(traverse_path[0], R_OK) < 0) die("No such file or directory"); 
			if (file_system != NULL) fts_close(file_system);
			file_system = fts_open(traverse_path, FTS_COMFOLLOW | FTS_NOCHDIR, &compare);
			parent = fts_read(file_system);
		}

		for(child = fts_children(file_system,0);
		    child != NULL; child = child->fts_link)
		{
			dot = strrchr((child->fts_name), '.');
			if (!(dot && !strcasecmp(dot, EXT))) continue;

			memset(track_path, 0, sizeof(track_path));
			sprintf(track_path,"%s/%s",child->fts_path,child->fts_name);

			extract_meta(meta,track_path);
			shout_set_metadata(shout, meta);
			
			shouterr = play_file(shout,track_path);
			if ((shouterr == SHOUTERR_UNCONNECTED) ||
			    (shouterr == SHOUTERR_SOCKET))
			{
				fprintf(stderr, "Failed to play %s\n", track_path);
				reconnect(shout);
				fprintf(stderr, "OK\n");
			}
		}

		parent = fts_read(file_system);
	}
}
