#include <stdio.h>
#include <limits.h>

#include "miniclip-xcb.h"

int main(void) {
  clip_t* clip;
  char buf[1024*1024*4];
  size_t size;

  clip = clip_init();
  if (clip == NULL) {
    printf("init failure\n");
    return -1;
  }

  for (;;) {
    size = sizeof buf;
    if (clip_wait(clip, buf, &size) == -1) {
      printf("failure");
      clip_uninit(clip);
      return -1;
    }

    printf("saw: %.*s\n", (int) (size & INT_MAX), buf);
  }

  clip_uninit(clip);

  return 0;
}
