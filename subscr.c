#include <stdio.h>
#include "dpiImpl.h"

void ObCallbackSubscr(void *context, dpiSubscrMessage *message);

void ObCallbackSubscrDebug(void *context, dpiSubscrMessage *message) {
	fprintf(stderr, "callback called\n");
	ObCallbackSubscr(context, message);
}
