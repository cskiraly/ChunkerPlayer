// chunkbuffer.c
// Author 
// Giuseppe Tropea
//
// Use the file compile to compile the program to build (assuming libavformat and libavcodec are 
// correctly installed your system).
//
// Run using
//
// ingestion myvideofile.mpg


#include "chunker_metadata.h"


/* Read config file for chunk strategy [numframes:num|size:num] and create a new chunk_buffer object
	numframes:num	fill each chunk with the same number of frame
	size:num	fill each chunk with the size of bytes no bigger than num
*/
struct chunker_metadata *chunkerInit() {
	ChunkerMetadata *cmeta=NULL;
	cfg_opt_t opts[] =
	{
		CFG_STR("strategyType", "frames", CFGF_NONE), //"frames" or "size"
		CFG_INT("strategyValue", 10, CFGF_NONE),
		CFG_STR("chunkID", "sequence", CFGF_NONE), //"sequence" or "starttime" or "monotonic"
		CFG_STR("outsideWorldUrl", "http://localhost:5557/externalplayer", CFGF_NONE),
		CFG_END()
	};
	cfg_t *cfg;

	fprintf(stderr, "CONFIG: Calling chunkerInit...\n");
	cmeta = (ChunkerMetadata *)malloc(sizeof(ChunkerMetadata));
	if(cmeta == NULL) {
		fprintf(stdout, "CONFIG: Error in memory for cmeta. Exiting.\n");
		exit(-1);
	}

	cfg = cfg_init(opts, CFGF_NONE);
	if(cfg_parse(cfg, "chunker.conf") == CFG_PARSE_ERROR) {
		fprintf(stdout, "CONFIG: Error in parsing config file chunker.conf. Exiting.\n");
		exit(-1);
	}

	cmeta->val_strategy = cfg_getint(cfg, "strategyValue");

	if(!(strcmp(cfg_getstr(cfg, "strategyType"), "frames"))) {
		// a fixed number of frames inside every chunk
		cmeta->strategy = 0;
		fprintf(stdout, "CONFIG: Will pack %d FRAMES in each chunk\n", cmeta->val_strategy);
	}
	else if(!(strcmp(cfg_getstr(cfg, "strategyType"), "size"))) {
		// each chunk of approx same size of bytes
		cmeta->strategy = 1;
		fprintf(stdout, "CONFIG: Will pack %d BYTES in each chunk\n", cmeta->val_strategy);
	}
	else {
		fprintf(stdout, "CONFIG: Unknown strategyType in config file chunker.conf. Exiting.\n");
		exit(-1);
	}

	if(!(strcmp(cfg_getstr(cfg, "chunkID"), "sequence"))) {
		// the chunkID is an increasing sequence of integers
		cmeta->cid = 0;
		fprintf(stdout, "CONFIG: Will give increasing SEQUENCE of integers as chunk IDs\n");
	}
	else if(!(strcmp(cfg_getstr(cfg, "chunkID"), "starttime"))) {
		// the chunkID is the chunk start time
		cmeta->cid = 1;
		fprintf(stdout, "CONFIG: Will give TIMESTAMP of start time as chunk IDs\n");
	}
	else if(!(strcmp(cfg_getstr(cfg, "chunkID"), "monotonic"))) {
		// the chunkID is always increasing also over different runs
		//because it's based on the gettimeofday()
		cmeta->cid = 2;
		fprintf(stdout, "CONFIG: Will give MONOTONIC INCREASING time of day as chunk IDs\n");
		struct timeval tv;
		uint64_t start_time;
		gettimeofday(&tv, NULL);
		start_time = tv.tv_usec + tv.tv_sec * 1000000ULL; //microseconds
		start_time /= 1000ULL; //milliseconds
		cmeta->base_chunkid_sequence_offset = start_time % INT_MAX; //TODO: verify 32/64 bit;
	}
	else {
		fprintf(stdout, "CONFIG: Unknown chunkID in config file chunker.conf. Exiting.\n");
		exit(-1);
	}

	strcpy(cmeta->outside_world_url, cfg_getstr(cfg, "outsideWorldUrl"));
	fprintf(stdout, "CONFIG: Chunk destination is %s\n", cmeta->outside_world_url);
	cfg_free(cfg);

	return cmeta;
}
