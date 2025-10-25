#ifndef FILE_OPS_H
#define FILE_OPS_H

#include "../queue/task_queue.h"

/* File operation handlers - currently placeholder for future refactoring */

int handle_upload(Task *t);
int handle_download(Task *t);
int handle_delete(Task *t);
int handle_list(Task *t);

#endif /* FILE_OPS_H */
