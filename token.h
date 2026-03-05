#ifndef GODROR_TOKEN
#define GODROR_TOKEN

struct godrorHwrap {
  uintptr_t handle;
  const char *token;
  const char *privateKey;
};
typedef struct godrorHwrap godrorHwrap;

void *ob_godrorWrapHandle(uintptr_t handle);
int godrorObTokenCallbackHandlerDebug(void* context, dpiAccessToken *token);

#endif
