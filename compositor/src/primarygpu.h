#ifndef ZACOS9_PRIMARYGPU_H
#define ZACOS9_PRIMARYGPU_H

/*
 * Which GPU renders. wlroots renders on the boot VGA device, whatever it is;
 * with a display on the motherboard's HDMI port the firmware makes the
 * integrated GPU the boot VGA device, so the discrete card's screens are fed
 * by a copy of every frame across GPUs (and lose their hardware cursor).
 *
 * primary_gpu_devices(largest) with largest false leaves that order alone and
 * returns NULL. With it true, the KMS devices are returned largest connected
 * screen first, as the colon-separated list WLR_DRM_DEVICES takes, so the
 * copy goes to the smaller screens; NULL when that is better left alone (one
 * card, or one already chosen). The copy only works between cards whose
 * renderers can import the other's buffers, which is why main.c asks for
 * linear buffers with it (see docs/hardware.md).
 */
char *primary_gpu_devices(bool largest);

#endif
