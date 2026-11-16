#ifndef __CONTROLPIPE_H__
#define __CONTROLPIPE_H__

#include <stdbool.h>
#include <stddef.h>

// Named pipe through which another process sends the commands of the headless mode, one per connection: the client
// writes a line, and reads the reply until the pipe is closed. Only on Windows

// Creates the pipe \\.\pipe\<name>. Returns false if it can't be created
bool ControlPipe_Open(const char* name);
// Waits for a client and reads its command, without the line end. Returns false if the pipe failed
bool ControlPipe_Receive(char* line, size_t size);
// Writes the reply to the client of the last command and closes its connection
void ControlPipe_Reply(const char* text);
void ControlPipe_Close(void);

#endif
