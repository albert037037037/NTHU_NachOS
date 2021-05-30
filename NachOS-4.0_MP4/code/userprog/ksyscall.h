/**************************************************************
 *
 * userprog/ksyscall.h
 *
 * Kernel interface for systemcalls 
 *
 * by Marcus Voelp  (c) Universitaet Karlsruhe
 *
 **************************************************************/

#ifndef __USERPROG_KSYSCALL_H__
#define __USERPROG_KSYSCALL_H__

#include "kernel.h"

#include "synchconsole.h"

void SysHalt()
{
	kernel->interrupt->Halt();
}

int SysAdd(int op1, int op2)
{
	return op1 + op2;
}

int SysCreate(char* name, int size){
	return kernel->fileSystem->Create(name, size);
}

OpenFileId SysOpen(char *name)
{
    if(kernel->fileSystem->Open(name) == NULL) return 0;
	else return 1;
}

int SysWrite(char *buffer, int size, OpenFileId id)
{
    return kernel->fileSystem->activeFile->Write(buffer, size);
}

int SysRead(char *buffer, int size, OpenFileId id)
{
    return kernel->fileSystem->activeFile->Read(buffer, size);
}

int SysClose(OpenFileId id)
{
    return kernel->fileSystem->CloseFile();
}

#ifdef FILESYS_STUB
int SysCreate(char *filename)
{
	// return value
	// 1: success
	// 0: failed
	return kernel->interrupt->CreateFile(filename);
}
#endif

#endif /* ! __USERPROG_KSYSCALL_H__ */
