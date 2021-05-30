// directory.cc
//	Routines to manage a directory of file names.
//
//	The directory is a table of fixed length entries; each
//	entry represents a single file, and contains the file name,
//	and the location of the file header on disk.  The fixed size
//	of each directory entry means that we have the restriction
//	of a fixed maximum size for file names.
//
//	The constructor initializes an empty directory of a certain size;
//	we use ReadFrom/WriteBack to fetch the contents of the directory
//	from disk, and to write back any modifications back to disk.
//
//	Also, this implementation has the restriction that the size
//	of the directory cannot expand.  In other words, once all the
//	entries in the directory are used, no more files can be created.
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#include "copyright.h"
#include "utility.h"
#include "filehdr.h"
#include "directory.h"
#include "debug.h"

//----------------------------------------------------------------------
// Directory::Directory
// 	Initialize a directory; initially, the directory is completely
//	empty.  If the disk is being formatted, an empty directory
//	is all we need, but otherwise, we need to call FetchFrom in order
//	to initialize it from disk.
//
//	"size" is the number of entries in the directory
//----------------------------------------------------------------------

Directory::Directory(int size)
{
    table = new DirectoryEntry[size];

    // MP4 mod tag
    memset(table, 0, sizeof(DirectoryEntry) * size); // dummy operation to keep valgrind happy

    tableSize = size;
    for (int i = 0; i < tableSize; i++){
        table[i].inUse = FALSE;
        table[i].isDir = FALSE;
    }
}

//----------------------------------------------------------------------
// Directory::~Directory
// 	De-allocate directory data structure.
//----------------------------------------------------------------------

Directory::~Directory()
{
    delete[] table;
}

//----------------------------------------------------------------------
// Directory::FetchFrom
// 	Read the contents of the directory from disk.
//
//	"file" -- file containing the directory contents
//----------------------------------------------------------------------

void Directory::FetchFrom(OpenFile *file)
{
    (void)file->ReadAt((char *)table, tableSize * sizeof(DirectoryEntry), 0);
}

//----------------------------------------------------------------------
// Directory::WriteBack
// 	Write any modifications to the directory back to disk
//
//	"file" -- file to contain the new directory contents
//----------------------------------------------------------------------

void Directory::WriteBack(OpenFile *file)
{
    (void)file->WriteAt((char *)table, tableSize * sizeof(DirectoryEntry), 0);
}

//----------------------------------------------------------------------
// Directory::FindIndex
// 	Look up file name in directory, and return its location in the table of
//	directory entries.  Return -1 if the name isn't in the directory.
//
//	"name" -- the file name to look up
//----------------------------------------------------------------------

int Directory::FindIndex(char *name)
{
    for (int i = 0; i < tableSize; i++)
        if (table[i].inUse && !strncmp(table[i].name, name, FileNameMaxLen))
            return i;
    return -1; // name not in directory
}

//----------------------------------------------------------------------
// Directory::Find
// 	Look up file name in directory, and return the disk sector number
//	where the file's header is stored. Return -1 if the name isn't
//	in the directory.
//
//	"name" -- the file name to look up
//----------------------------------------------------------------------

int Directory::Find(char *name)
{
    int i = FindIndex(name);

    if (i != -1)
        return table[i].sector;
    return -1;
}

int Directory::FindisDir(char* name){
    int i = FindIndex(name);

    if (i != -1)
        return table[i].isDir;
    return -1;
}

//----------------------------------------------------------------------
// Directory::Add
// 	Add a file into the directory.  Return TRUE if successful;
//	return FALSE if the file name is already in the directory, or if
//	the directory is completely full, and has no more space for
//	additional file names.
//
//	"name" -- the name of the file being added
//	"newSector" -- the disk sector containing the added file's header
//----------------------------------------------------------------------

bool Directory::Add(char *name, int newSector, bool AddDir)
{
    if (FindIndex(name) != -1)
        return FALSE;

    for (int i = 0; i < tableSize; i++)
        if (!table[i].inUse)
        {
            table[i].inUse = TRUE;
            table[i].isDir = (AddDir) ? TRUE : FALSE;
            strncpy(table[i].name, name, FileNameMaxLen);
            table[i].sector = newSector;
            return TRUE;
        }
    return FALSE; // no space.  Fix when we have extensible files.
}

//----------------------------------------------------------------------
// Directory::Remove
// 	Remove a file name from the directory.  Return TRUE if successful;
//	return FALSE if the file isn't in the directory.
//
//	"name" -- the file name to be removed
//----------------------------------------------------------------------

bool Directory::Remove(char *name)
{
    int i = FindIndex(name);

    if (i == -1)
        return FALSE; // name not in directory
    table[i].inUse = FALSE;
    return TRUE;
}

//----------------------------------------------------------------------
// Directory::RecursiveRemove
// 	Remove a file name from the directory.  Return TRUE if successful;
//	return FALSE if the file isn't in the directory.
//
//	"name" -- the file name to be removed
//----------------------------------------------------------------------

void Directory::RecursiveRemove(PersistentBitmap *freeMap)
{
    int i;
    for(int i=0; i<tableSize; i++){
        if(table[i].inUse){
            DEBUG(dbgKYL, "table[" << i << "] is " << table[i].name);
        }
    }
    DEBUG(dbgKYL, "END of a dir");
    for(i=0; i<tableSize; i++){
        if(table[i].inUse && !table[i].isDir){
            int sector = table[i].sector;
            FileHeader* hdr = new FileHeader;
            hdr->FetchFrom(sector);
            hdr->Deallocate(freeMap);
            freeMap->Clear(sector); 
            Remove(table[i].name);
        }
        else if(table[i].inUse && table[i].isDir){
            int sector = table[i].sector;
            OpenFile* next_dir_hdr = new OpenFile(sector);
            Directory* next_level_dir = new Directory(NumDirEntries);
            next_level_dir->FetchFrom(next_dir_hdr);
            next_level_dir->RecursiveRemove(freeMap);
            next_level_dir->WriteBack(next_dir_hdr);

            FileHeader* hdr = new FileHeader;
            hdr->FetchFrom(sector);
            hdr->Deallocate(freeMap);
            freeMap->Clear(sector);
            Remove(table[i].name);
            delete next_dir_hdr;
            delete next_level_dir;
            delete hdr;
        }
    }
}

//----------------------------------------------------------------------
// Directory::List
// 	List all the file names in the directory.
//----------------------------------------------------------------------

void Directory::List()
{
    for (int i = 0; i < tableSize; i++)
        if (table[i].inUse) {
            if (table[i].isDir)
                printf("%s%s\n", "{D}: ", table[i].name);
            else
                printf("%s%s\n", "{F}: ", table[i].name);
    }
}

bool Directory::IsDir(char* name){
    int i = FindIndex(name);
    return table[i].isDir;
}

//----------------------------------------------------------------------
// Directory::RecursiveList
// 	List all the file names in the directory recursively.
//----------------------------------------------------------------------

void Directory::RecursiveList(int num)
{
    for (int i = 0; i < tableSize; i++){
        if (table[i].inUse){
            if(num != 0){
                for(int i=0; i<num; i++) printf("%c", '  ');
                if (table[i].isDir)
                    printf("%s%s\n", "{D}: ", table[i].name);
                else
                    printf("%s%s\n", "{F}: ", table[i].name);
            }
            else {
                if (table[i].isDir)
                    printf("%s%s\n", "{D}: ", table[i].name);
                else
                    printf("%s%s\n", "{F}: ", table[i].name);
            } 
        }
        if(table[i].isDir){
            int sector = table[i].sector;
            OpenFile* next_dir_hdr = new OpenFile(sector);
            Directory* next_dir = new Directory(NumDirEntries);
            next_dir->FetchFrom(next_dir_hdr);
            next_dir->RecursiveList(num+1);
            delete next_dir;
            delete next_dir_hdr;
        }
    }     
}


//----------------------------------------------------------------------
// Directory::Print
// 	List all the file names in the directory, their FileHeader locations,
//	and the contents of each file.  For debugging.
//----------------------------------------------------------------------

void Directory::Print()
{
    FileHeader *hdr = new FileHeader;

    printf("Directory contents:\n");
    for (int i = 0; i < tableSize; i++)
        if (table[i].inUse)
        {
            printf("Name: %s, Sector: %d\n", table[i].name, table[i].sector);
            hdr->FetchFrom(table[i].sector);
            hdr->Print();
        }
    printf("\n");
    delete hdr;
}

void Directory::PrintUse()
{
    FileHeader *hdr = new FileHeader;
    for (int i = 0; i < tableSize; i++){
        if (table[i].inUse)
        {
            if(table[i].isDir){
                OpenFile* tmp = new OpenFile(table[i].sector);
                Directory *directory = new Directory(NumDirEntries);
                directory->FetchFrom(tmp);
                directory->PrintUse();
                delete directory;
                delete tmp;
            }
            else{
                printf("Name: %s, Sector: %d ", table[i].name, table[i].sector);
                hdr->FetchFrom(table[i].sector);
                hdr->PrintUse();
                printf("\n");
            }
            
        }
    }
    delete hdr;
}
