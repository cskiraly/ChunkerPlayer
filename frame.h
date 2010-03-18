#ifndef _FRAME_H
#define _FRAME_H

/**
 * @file frame.h
 * (giuseppe tropea)
 *
 * Contains description of the Frame header
 * and Frame related functions
 * @todo elaborate the description
 */

typedef struct Frame
{
  /**
   * Sequential number of this frame.
   * The sequential number assigned to this frame by the encoder (starts at zero)
   */
  int number;

  /**
   * The time at which the live source emitted this frame.
   */
  struct timeval timestamp;

  int64_t pts;

  /**
   * the size in bytes of this frame.
   */
  int size;

  /**
   * Frame type.
   * Types are (A,I,P,B) (audio, I, P, B)
   */
  int type;
} Frame;

#endif

