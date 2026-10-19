#ifndef MOONLIGHT_VITA_SYSTEM_VIDEO_H
#define MOONLIGHT_VITA_SYSTEM_VIDEO_H

#include <psp2/videodec.h>

/* SDK allocation attributes: contiguous storage plus explicit alignment. */
#define VITA_SYSTEM_VIDEO_MEM_ATTR 0x00200004u
#define VITA_SYSTEM_VIDEO_MEM_ALIGNMENT 0x100000u

int vita_system_video_create(
    const SceVideodecQueryInitInfoHwAvcdec *init_info,
    SceAvcdecCtrl **decoder);
int vita_system_video_destroy(void);

#endif
