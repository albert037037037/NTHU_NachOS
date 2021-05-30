// filehdr.cc
//	Routines for managing the disk file header (in UNIX, this
//	would be called the i-node).
//
//	The file header is used to locate where on disk the
//	file's data is stored.  We implement this as a fixed size
//	table of pointers -- each entry in the table points to the
//	disk sector containing that portion of the file data
//	(in other words, there are no indirect or doubly indirect
//	blocks). The table size is chosen so that the file header
//	will be just big enough to fit in one disk sector,
//
//      Unlike in a real system, we do not keep track of file permissions,
//	ownership, last modification date, etc., in the file header.
//
//	A file header can be initialized in two ways:
//	   for a new file, by modifying the in-memory data structure
//	     to point to the newly allocated data blocks
//	   for a file already on disk, by reading the file header from disk
//
// Copyright (c) 1992-1993 The Regents of the University of California.
// All rights reserved.  See copyright.h for copyright notice and limitation
// of liability and disclaimer of warranty provisions.

#include "copyright.h"

#include "filehdr.h"
#include "debug.h"
#include "synchdisk.h"
#include "main.h"

#define OneLevelSize (30 * 128)
#define TwoLevelSize (30 * 30 * 128)
#define ThreeLevelSize (30 * 30 * 30 * 128)

//----------------------------------------------------------------------
// MP4 mod tag
// FileHeader::FileHeader
//	There is no need to initialize a fileheader,
//	since all the information should be initialized by Allocate or FetchFrom.
//	The purpose of this function is to keep valgrind happy.
//----------------------------------------------------------------------
FileHeader::FileHeader()
{
	numBytes = -1;
	numSectors = -1;
	memset(dataSectors, -1, sizeof(dataSectors));
}

//----------------------------------------------------------------------
// MP4 mod tag
// FileHeader::~FileHeader
//	Currently, there is not need to do anything in destructor function.
//	However, if you decide to add some "in-core" data in header
//	Always remember to deallocate their space or you will leak memory
//----------------------------------------------------------------------
FileHeader::~FileHeader()
{
	// nothing to do now
}

//----------------------------------------------------------------------
// FileHeader::Allocate
// 	Initialize a fresh file header for a newly created file.
//	Allocate data blocks for the file out of the map of free disk blocks.
//	Return FALSE if there are not enough free blocks to accomodate
//	the new file.
//
//	"freeMap" is the bit map of free disk sectors
//	"fileSize" is the bit map of free disk sectors
//----------------------------------------------------------------------

bool FileHeader::Allocate(PersistentBitmap *freeMap, int fileSize)
{
	numBytes = fileSize;
	numSectors = divRoundUp(fileSize, SectorSize);
	if (freeMap->NumClear() < numSectors)
		return FALSE; // not enough space

	
	if(fileSize > ThreeLevelSize){
		int i = 0;
		while(fileSize > 0)
		{
			dataSectors[i] = freeMap->FindAndSet();
			ASSERT(dataSectors[i] >= 0);

			FileHeader *next_level_hdr = new FileHeader;

			if (fileSize > ThreeLevelSize)
			{
				bool success = next_level_hdr->Allocate(freeMap, ThreeLevelSize);
				if(!success) return FALSE; // space is not enough
				fileSize -= ThreeLevelSize;
			}
			else
			{
				bool success = next_level_hdr->Allocate(freeMap, fileSize);
				if(!success) return FALSE; // space is not enough
				fileSize -= fileSize;
			}
			next_level_hdr->WriteBack(dataSectors[i]);
			delete next_level_hdr;
			i++;
		}
		numSectors = i;
	}
	else if(fileSize > TwoLevelSize){
		int i = 0;
		while(fileSize > 0)
		{
			dataSectors[i] = freeMap->FindAndSet();
			ASSERT(dataSectors[i] >= 0);

			FileHeader *next_level_hdr = new FileHeader;

			if (fileSize > TwoLevelSize)
			{
				bool success = next_level_hdr->Allocate(freeMap, TwoLevelSize);
				if(!success) return FALSE; // space is not enough
				fileSize -= TwoLevelSize;
			}
			else
			{
				bool success = next_level_hdr->Allocate(freeMap, fileSize);
				if(!success) return FALSE; // space is not enough
				fileSize -= fileSize;
			}
			next_level_hdr->WriteBack(dataSectors[i]);
			delete next_level_hdr;
			i++;
		}
		numSectors = i;
	}
	else if(fileSize > OneLevelSize){
		int i = 0;
		while(fileSize > 0)
		{
			dataSectors[i] = freeMap->FindAndSet();
			ASSERT(dataSectors[i] >= 0);

			FileHeader *next_level_hdr = new FileHeader;

			if (fileSize > OneLevelSize)
			{
				bool success = next_level_hdr->Allocate(freeMap, OneLevelSize);
				if(!success) return FALSE; // space is not enough
				fileSize -= OneLevelSize;
			}
			else
			{
				bool success = next_level_hdr->Allocate(freeMap, fileSize);
				if(!success) return FALSE; // space is not enough
				fileSize -= fileSize;
			}
			next_level_hdr->WriteBack(dataSectors[i]);
			delete next_level_hdr;
			i++;
		}
		numSectors = i;

		// for debug
		DEBUG(dbgKYL, "Root =========================");
		for(int i=0; i<numSectors; i++){
			DEBUG(dbgKYL, "sector num is " << dataSectors[i]);
		}
	}
	else{
		for (int j = 0; j < numSectors; j++)
		{
			dataSectors[j] = freeMap->FindAndSet();
			// since we checked that there was enough free space,
			// we expect this to succeed
			ASSERT(dataSectors[j] >= 0);
		}

		// for debug
		for(int j=0; j<numSectors; j++){
			DEBUG(dbgKYL, "sector num is " << dataSectors[j]);
		}
		DEBUG(dbgKYL, "End ===============================");
	}

	
	return TRUE;
}

//----------------------------------------------------------------------
// FileHeader::Deallocate
// 	De-allocate all the space allocated for data blocks for this file.
//
//	"freeMap" is the bit map of free disk sectors
//----------------------------------------------------------------------

void FileHeader::Deallocate(PersistentBitmap *freeMap)
{
	if(numBytes > OneLevelSize){
		for (int i = 0; i < numSectors; i++)
		{
			FileHeader *hdr = new FileHeader;
			hdr->FetchFrom(dataSectors[i]);
			hdr->Deallocate(freeMap);
			freeMap->Clear((int)dataSectors[i]);
			delete hdr;
		}
	}
	else{
		for (int i = 0; i < numSectors; i++)
		{
			ASSERT(freeMap->Test((int)dataSectors[i])); // ought to be marked!
			freeMap->Clear((int)dataSectors[i]);
		}
	}
}

//----------------------------------------------------------------------
// FileHeader::FetchFrom
// 	Fetch contents of file header from disk.
//
//	"sector" is the disk sector containing the file header
//----------------------------------------------------------------------

void FileHeader::FetchFrom(int sector)
{
	kernel->synchDisk->ReadSector(sector, (char *)this);

	/*
		MP4 Hint:
		After you add some in-core informations, you will need to rebuild the header's structure
	*/
}

//----------------------------------------------------------------------
// FileHeader::WriteBack
// 	Write the modified contents of the file header back to disk.
//
//	"sector" is the disk sector to contain the file header
//----------------------------------------------------------------------

void FileHeader::WriteBack(int sector)
{
	kernel->synchDisk->WriteSector(sector, (char *)this);

	/*
		MP4 Hint:
		After you add some in-core informations, you may not want to write all fields into disk.
		Use this instead:
		char buf[SectorSize];
		memcpy(buf + offset, &dataToBeWritten, sizeof(dataToBeWritten));
		...
	*/
}

//----------------------------------------------------------------------
// FileHeader::ByteToSector
// 	Return which disk sector is storing a particular byte within the file.
//      This is essentially a translation from a virtual address (the
//	offset in the file) to a physical address (the sector where the
//	data at the offset is stored).
//
//	"offset" is the location within the file of the byte in question
//----------------------------------------------------------------------

int FileHeader::ByteToSector(int offset)
{
	if(numBytes > ThreeLevelSize){
		FileHeader *hdr = new FileHeader;
		int sector = divRoundDown(offset, ThreeLevelSize);
		hdr->FetchFrom(dataSectors[sector]);
		int value = hdr->ByteToSector(offset - sector * ThreeLevelSize);
		delete hdr;
		return value;
	}
	else if(numBytes > TwoLevelSize){
		FileHeader *hdr = new FileHeader;
		int sector = divRoundDown(offset, TwoLevelSize);
		hdr->FetchFrom(dataSectors[sector]);
		int value = hdr->ByteToSector(offset - sector * TwoLevelSize);
		delete hdr;
		return value;
	}
	else if (numBytes > OneLevelSize)
	{
		FileHeader *hdr = new FileHeader;
		int sector = divRoundDown(offset, OneLevelSize);
		hdr->FetchFrom(dataSectors[sector]);
		int value = hdr->ByteToSector(offset - sector * OneLevelSize);
		delete hdr;
		return value;
	}
	else
	{
		return (dataSectors[offset / SectorSize]);
	}
}

//----------------------------------------------------------------------
// FileHeader::FileLength
// 	Return the number of bytes in the file.
//----------------------------------------------------------------------

int FileHeader::FileLength()
{
	return numBytes;
}

//----------------------------------------------------------------------
// FileHeader::Print
// 	Print the contents of the file header, and the contents of all
//	the data blocks pointed to by the file header.
//----------------------------------------------------------------------

void FileHeader::Print()
{
	int i, j, k;
	char *data = new char[SectorSize];

	printf("FileHeader contents.  File size: %d.  File blocks:\n", numBytes);

	if (numBytes > OneLevelSize){
		for (i = 0; i < numSectors; i++)
		{
			FileHeader *hdr = new FileHeader;
			hdr->FetchFrom(dataSectors[i]);
			hdr->Print();
		}
	}
	else{
		for (i = 0; i < numSectors; i++)
		printf("%d ", dataSectors[i]);
		printf("\nFile contents:\n");
		for (i = k = 0; i < numSectors; i++)
		{
			kernel->synchDisk->ReadSector(dataSectors[i], data);
			for (j = 0; (j < SectorSize) && (k < numBytes); j++, k++)
			{
				if ('\040' <= data[j] && data[j] <= '\176') // isprint(data[j])
					printf("%c", data[j]);
				else
					printf("\\%x", (unsigned char)data[j]);
			}
			printf("\n");
		}
		delete[] data;
	}
}

void FileHeader::PrintUse(){
	int i;
	if (numBytes > OneLevelSize){
		for (i = 0; i < numSectors; i++)
		{
			printf("%d ", dataSectors[i]);
			FileHeader *hdr = new FileHeader;
			hdr->FetchFrom(dataSectors[i]);
			hdr->PrintUse();
		}
	}
}
