#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <fts.h>
#include <sys/stat.h>
#include <shout/shout.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>

shout_t *shout;
shout_metadata_t *meta;

int
compare(const FTSENT** one, const FTSENT** two)
{
	if(S_ISDIR((*one)->fts_statp->st_mode) &&
	   S_ISDIR((*two)->fts_statp->st_mode))
		return(rand());

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
			break;
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
		fputc('\n', stderr);

	exit(1);
}
void
reconnect(shout_t *shout)
{
	int i;

	fprintf(stderr,"Disconnected, trying to reconnect...\n");
	
	for(i=0; i>=3; i++)
		if(shout_open(shout) == SHOUTERR_SUCCESS) return;

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
	sigaction(SIGINT, &sigact, (struct sigaction *)NULL);

	shout_init();
	shout = shout_new();
	meta = shout_metadata_new();
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
			if (!(dot && !strcasecmp(dot, ".mp3"))) continue;

			memset(track_path, 0, sizeof(track_path));
			sprintf(track_path,"%s/%s",child->fts_path,child->fts_name);

			extract_meta(meta,track_path);
			shout_set_metadata(shout, meta);
			if(play_file(shout,track_path) < 0)
				fprintf(stderr, "Failed to play %s\n", track_path);

			if(shout_get_errno(shout) == SHOUTERR_UNCONNECTED)
				reconnect(shout);
		}

		parent = fts_read(file_system);
	}
}