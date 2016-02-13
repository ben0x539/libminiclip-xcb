#ifndef INCLUDED_MINICLIP_XCB_H
#define INCLUDED_MINICLIP_XCB_H

#include <stddef.h>

typedef struct clip_t clip_t;

clip_t* clip_init(void);
void clip_uninit(clip_t* clip);

int clip_poll(clip_t* clip, char* out, size_t* size);
int clip_wait(clip_t* clip, char* out, size_t* size);

int clip_check_error(clip_t* clip);

#endif
