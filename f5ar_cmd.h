#ifndef F5C_CMD_H
#define F5C_CMD_H

#ifdef __cplusplus
extern "C" {
#endif

/*
* For those who want "cmd tool"-like functionality in the form of a library
* argv[0] should contain whatever your program name is or be empty
* Set verbose to 0 if you don't want to see standard terminal output
*
* Returns one of the F5AR error codes defined in f5c.h
*/
int f5ar_cmd_exec(int argc, char *argv[], int verbose);

#ifdef __cplusplus
}
#endif

#endif
