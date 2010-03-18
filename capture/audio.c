#include "audio.h"
#include "capture.h"

int init_audio(char* audio_dev_name)
{
    /* Open PCM device for recording (capture). */
    if ((err = snd_pcm_open (&capture_handle, audio_dev_name, SND_PCM_STREAM_CAPTURE, 0)) < 0)
    {
        fprintf (stderr, "cannot open audio device %s (%s)\n", audio_dev_name, snd_strerror (err));
        return err;
    }
    /* Allocate a hardware parameters object. */
    snd_pcm_hw_params_alloca(&hw_params);

    /* Fill it in with default values. */
    snd_pcm_hw_params_any(capture_handle, hw_params);

    /* Set the desired hardware parameters. */

    /* Interleaved mode */
    snd_pcm_hw_params_set_access(capture_handle, hw_params, AUDIO_INTERLEAVED);

    /* Signed 16-bit little-endian format */
    snd_pcm_hw_params_set_format(capture_handle, hw_params, AUDIO_FORMAT);

    /* Two channels (stereo) */
    snd_pcm_hw_params_set_channels(capture_handle, hw_params, AUDIO_CHANNELS);

    /* bits/second sampling rate */
    int val = AUDIO_SAMPLE_RATE;
    int dir;
    snd_pcm_hw_params_set_rate_near(capture_handle, hw_params, &val, &dir);

    /* Set period size to AUDIO_PERIOD frames. */
    frames = AUDIO_PERIOD;
    snd_pcm_hw_params_set_period_size_near(capture_handle, hw_params, &frames, &dir);

    /* Write the parameters to the driver */
    err = snd_pcm_hw_params(capture_handle, hw_params);
    if (err < 0) {
        fprintf(stderr, "unable to set hw parameters: %s\n", snd_strerror(err));
        return err;
    }

    /* Use a buffer large enough to hold one period */
    snd_pcm_hw_params_get_period_size(hw_params, &frames, &dir);
    buffer_size = frames * 4; /* 2 bytes/sample, 2 channels */
    audio_buffer = (char *) malloc(buffer_size);

    snd_pcm_hw_params_get_period_time(hw_params, &val, &dir);

    audio_s_bit = snd_pcm_hw_params_get_sbits(hw_params);
}

void read_sample()
{
    int rc = snd_pcm_readi(capture_handle, audio_buffer, frames);
    if (rc == -EPIPE) {
      /* EPIPE means overrun */
      fprintf(stderr, "overrun occurred\n");
      snd_pcm_prepare(capture_handle);
    } else if (rc < 0) {
      fprintf(stderr, "error from read: %s\n", snd_strerror(rc));
    } else if (rc != (int)frames) {
      fprintf(stderr, "short read, read %d frames\n", rc);
    }
    else
    {
        //rc = write(1, audio_buffer, buffer_size);
        //if (rc != buffer_size) fprintf(stderr, "short write: wrote %d bytes\n", rc);

        int gotbytes = snd_pcm_frames_to_bytes(capture_handle, rc);
        process_sample(audio_buffer, gotbytes);
    }
}

void close_audio_device()
{
    snd_pcm_drain(capture_handle);
    snd_pcm_close(capture_handle);
    free(audio_buffer);
}