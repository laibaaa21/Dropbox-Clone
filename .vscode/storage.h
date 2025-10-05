#ifndef STORAGE_H
#define STORAGE_H

#include "taskqueue.h"

int handle_upload(Task *t);
int handle_download(Task *t);
int handle_delete(Task *t);
int handle_list(Task *t);

#endif
