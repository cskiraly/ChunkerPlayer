/*
 *  Copyright (c) 2009-2011 Carmelo Daniele, Dario Marchese, Diego Reforgiato, Giuseppe Tropea
 *  developed for the Napa-Wine EU project. See www.napa-wine.eu
 *
 *  This is free software; see lgpl-2.1.txt
 */

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
		CFG_INT("audioFramesPerChunk", 1, CFGF_NONE),
		CFG_INT("videoFramesPerChunk", 1, CFGF_NONE),
		CFG_INT("targetChunkSize", 1024, CFGF_NONE),
		CFG_STR("chunkID", "sequence", CFGF_NONE), //"sequence" or "starttime" or "monotonic"
		CFG_STR("outsideWorldUrl", "http://localhost:5557/externalplayer", CFGF_NONE),
		CFG_END()
	};
	cfg_t *cfg;

	fprintf(stderr, "CONFIG: Calling chunkerInit...\n");
	cmeta = (ChunkerMetadata *)malloc(sizeof(ChunkerMetadata));
	if(cmeta == NULL) {
		fprintf(stderr, "CONFIG: Error in memory for cmeta. Exiting.\n");
		exit(-1);
	}

	cfg = cfg_init(opts, CFGF_NONE);
	if(cfg_parse(cfg, "chunker.conf") == CFG_PARSE_ERROR) {
		fprintf(stderr, "CONFIG: Error in parsing config file chunker.conf. Exiting.\n");
		exit(-1);
	}

	if(!(strcmp(cfg_getstr(cfg, "strategyType"), "frames"))) {
		// a fixed number of frames inside every chunk
		cmeta->strategy = 0;
		cmeta->framesPerChunk[0] = cfg_getint(cfg, "audioFramesPerChunk");
		cmeta->framesPerChunk[1] = cfg_getint(cfg, "videoFramesPerChunk");
		fprintf(stderr, "CONFIG: Will pack %d AUDIO FRAMES or %d VIDEO FRAMES in each chunk\n", cmeta->framesPerChunk[0], cmeta->framesPerChunk[1]);
	}
	else if(!(strcmp(cfg_getstr(cfg, "strategyType"), "size"))) {
		// each chunk of approx same size of bytes
		cmeta->strategy = 1;
		cmeta->targetChunkSize = cfg_getint(cfg, "targetChunkSize");
		fprintf(stderr, "CONFIG: Will pack %d BYTES in each chunk\n", cmeta->targetChunkSize);
	}
	else {
		fprintf(stderr, "CONFIG: Unknown strategyType in config file chunker.conf. Exiting.\n");
		exit(-1);
	}

	if(!(strcmp(cfg_getstr(cfg, "chunkID"), "sequence"))) {
		// the chunkID is an increasing sequence of integers
		cmeta->cid = 0;
		fprintf(stderr, "CONFIG: Will give increasing SEQUENCE of integers as chunk IDs\n");
	}
	else if(!(strcmp(cfg_getstr(cfg, "chunkID"), "starttime"))) {
		// the chunkID is the chunk start time
		cmeta->cid = 1;
		fprintf(stderr, "CONFIG: Will give TIMESTAMP of start time as chunk IDs\n");
	}
	else if(!(strcmp(cfg_getstr(cfg, "chunkID"), "monotonic"))) {
		// the chunkID is always increasing also over different runs
		//because it's based on the gettimeofday()
		cmeta->cid = 2;
		fprintf(stderr, "CONFIG: Will give MONOTONIC INCREASING time of day as chunk IDs\n");
		struct timeval tv;
		uint64_t start_time;
		gettimeofday(&tv, NULL);
		start_time = tv.tv_usec + tv.tv_sec * 1000000ULL; //microseconds
		start_time /= 1000ULL; //milliseconds
		cmeta->base_chunkid_sequence_offset = start_time % INT_MAX; //TODO: verify 32/64 bit;
	}
	else {
		fprintf(stderr, "CONFIG: Unknown chunkID in config file chunker.conf. Exiting.\n");
		exit(-1);
	}

	strcpy(cmeta->outside_world_url, cfg_getstr(cfg, "outsideWorldUrl"));
	fprintf(stderr, "CONFIG: Chunk destination is %s\n", cmeta->outside_world_url);
	cfg_free(cfg);

	return cmeta;
}
