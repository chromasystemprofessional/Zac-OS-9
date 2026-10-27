#ifndef ZACOS9_PRIMARYGPU_H
#define ZACOS9_PRIMARYGPU_H

/*
 * Which GPU renders. wlroots renders on the boot VGA device, whatever it is.
 * With a display on the motherboard's HDMI port the firmware makes the
 * integrated GPU the boot VGA device, so the discrete card's screens were
 * fed by a copy of every frame across GPUs (and lost their hardware cursor).
 *
 * primary_gpu_devices() returns the KMS devices to use, largest connected
 * screen first, as the colon-separated list WLR_DRM_DEVICES takes; NULL when
 * the choice is better left alone (one card, or one already made).
 */
char *primary_gpu_devices(void);

#endif
