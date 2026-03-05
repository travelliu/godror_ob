#include <stdint.h>
#include "dpiImpl.h"
#include "token.h"

void *ob_godrorWrapHandle(uintptr_t handle) {
  godrorHwrap *a1 = calloc(1, sizeof(godrorHwrap));
  a1->handle = handle;
  return (void *)a1;
}

void ObTokenCallbackHandler(uintptr_t handle, dpiAccessToken *access_token);

int godrorObTokenCallbackHandlerDebug(void *context, dpiAccessToken *acToken) {
  godrorHwrap *a1 = (godrorHwrap*)context;
  if (a1->token) {
    free((void *)a1->token);
  }
  if (a1->privateKey) {
    free((void *)a1->privateKey);
  }
  ObTokenCallbackHandler(a1->handle, acToken);
  a1->token = acToken->token;
  a1->privateKey = acToken->privateKey;
  return 0;
}
