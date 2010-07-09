#ifndef _EXTERNAL_CHUNK_H
#define _EXTERNAL_CHUNK_H

#include <sys/time.h>
#include <sys/types.h>
#include <stdint.h>

/** 
 * @file external_chunk.h
 *
 * Chunk structure & manipulation routines.
 * Describes the structure of the chunk
 *
 * @todo Describe external interfaces (accessor fns) here in detail.
 */


/** A Chunk is the basic data unit in P2P transfer @todo further refine */
typedef struct {
        /**
         * The sequential number of this chunk, starting from 0.
         * (although i would rather prefer to call this ID
         * (giuseppe tropea)
         */
        int32_t seq;

//        /** Presentation timestamp */
//        obsoleted by the start and end times
//        (giuseppe tropea)
//        struct timeval pts;

        /** How many frames are in this chunk.
         * (giuseppe tropea)
         */
        int32_t frames_num;

        /**
         * The timestamp of the first frame in the chunk.
         * This timestamp has to be in milliseconds resolution
         * (giuseppe tropea)
         */
        struct timeval start_time;

        /**
         * The timestamp of the last frame in the chunk.
         * This timestamp has to be in milliseconds resolution
         * (giuseppe tropea)
         */
        struct timeval end_time;

        /**
         * Length of the payload (essentially the frames data) in bytes.
         * payload_length + sizeof(Chunk) makes the chunk length
         * (giuseppe tropea)
         */
        int32_t payload_len;

        /**
         * Length in bytes.
         * (although this might be redundant since we have payload_len which is more useful
         * (giuseppe tropea)
         */
        int32_t len;

        /**
         * An integer representing the class or group of this chunk.
         * used for example in the layered and MDC coding
         * (giuseppe tropea)
         */
        int32_t category;

        /**
         * A double float representing the priority of this chunk.
         * used for example for a base layer in an SVC coding, or for audio chunks
         * (giuseppe tropea)
         */
        double priority;

//        /** Payload */
//        the payload should **not** be a separated memory area linked here,
//        to avoid memcopy when sending on the wire (just like skbuff in linux TCP).
//        Payload follows instead the header at position sizeof(Chunk)+1
//        (giuseppe tropea)
        uint8_t *data;

        /** Internal reference counter */
        int _refcnt;
} ExternalChunk;

#endif


