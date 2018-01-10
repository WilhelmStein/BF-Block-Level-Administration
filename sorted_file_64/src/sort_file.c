#include "sort_file.h"
#include "bf.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define CALL_OR_EXIT(call)		\
{                           	\
	BF_ErrorCode code = call; 	\
	if(code != BF_OK) {       	\
		BF_PrintError(code);    \
		return SR_BF_ERROR;		\
	}                         	\
}

// Utility Function:
// Assumes the file has been already opened
// Accesses the file's metadata block at index META
// Checks if its identifier corresponds to that of sorted file
static bool isSorted(const int fileDesc)
{
	BF_Block * block;
	BF_Block_Init(&block);

	CALL_OR_EXIT(BF_GetBlock(fileDesc, META, block));
	char * blockData = BF_Block_GetData(block);

	bool rv = (blockData[IDENTIFIER] == SORTED);
	CALL_OR_EXIT(BF_UnpinBlock(block));

	BF_Block_Destroy(&block);

	return rv;
}

SR_ErrorCode SR_Init() 
{

  	return SR_OK;
}


SR_ErrorCode SR_CreateFile(const char *fileName) 
{

	CALL_OR_EXIT(BF_CreateFile(fileName));

	int fileDesc;
	BF_Block *block;
	BF_Block_Init(&block);

	CALL_OR_EXIT(BF_OpenFile(fileName, &fileDesc));
	CALL_OR_EXIT(BF_AllocateBlock(fileDesc, block));
	char *data = BF_Block_GetData(block);

	data[IDENTIFIER] = SORTED;

	BF_Block_SetDirty(block);
	CALL_OR_EXIT(BF_UnpinBlock(block));
	BF_Block_Destroy(&block);
	CALL_OR_EXIT(BF_CloseFile(fileDesc));

  	return SR_OK;
}


SR_ErrorCode SR_OpenFile(const char *fileName, int *fileDesc)
{
	CALL_OR_EXIT(BF_OpenFile(fileName, fileDesc));
	if (!isSorted(*fileDesc))
	{
		CALL_OR_EXIT(BF_Close(fileName, fileDesc));
		return SR_UNSORTED;
	}

	return SR_OK;
}

SR_ErrorCode SR_CloseFile(int fileDesc)
{
	if (!isSorted(fileDesc))
		return SR_UNSORTED;

	CALL_OR_EXIT(BF_CloseFile(fileDesc));

	return SR_OK;
}

SR_ErrorCode SR_InsertEntry(int fileDesc,	Record record) 
{
	if (!isSorted(fileDesc)) 
		return SR_UNSORTED;

	BF_Block *block;
	BF_Block_Init(&block);

	int blocksNum;
	CALL_OR_EXIT(BF_GetBlockCounter(fileDesc, &blocksNum));
	CALL_OR_EXIT(BF_GetBlock(fileDesc, blocksNum - 1, block));
	char *data = BF_Block_GetData(block);

	
	if(blocksNum == 1 || (int)data[RECORDS] == MAXRECORDS) {
		BF_Block *newBlock;
		BF_Block_Init(&newBlock);
		CALL_OR_EXIT(BF_AllocateBlock(fileDesc, newBlock));
		data = BF_Block_GetData(newBlock);

		int one = 1;
		memcpy((int *)&data[RECORDS], &one, sizeof(int));

		memcpy((Record *)&data[RECORD(0)], &record, sizeof(Record));

		BF_Block_SetDirty(newBlock);
		BF_UnpinBlock(newBlock);
		BF_Block_Destroy(&newBlock);
	}
	else if (blocksNum != 1) {

		int records = (int)data[RECORDS];
		
		memcpy((Record *)&data[RECORD(records)], &record, sizeof(Record));

		records += 1;
		memcpy((int *)&data[RECORDS], &records, sizeof(int));

		BF_Block_SetDirty(block);
	}

	BF_UnpinBlock(block);
	BF_Block_Destroy(&block);

  	return SR_OK;
}

static bool compareRecord(const Record * const ra, const Record * const rb, const int fieldNo)
{
	switch(fieldNo)
	{
		case 0 :
			return (ra->id < rb->id);
		case 1 :
			return (strcmp(ra->name, rb->name) < 0);
		case 2 :
			return (strcmp(ra->surname, rb->surname) < 0);
		default:
			return (strcmp(ra->city, rb->city) < 0);
	}
}

static void swapRecord(Record * const ra, Record * const rb)
{
	if (ra != rb)
	{
		Record rac;
		
		memcpy((void *) &rac, (void *)   ra, sizeof(Record));
		memcpy((void *)   ra, (void *)   rb, sizeof(Record));
		memcpy((void *)   rb, (void *) &rac, sizeof(Record));
	}
}

static Record * getRecord(char * const blockData[], const int index)
{
	int blockIndex = index / MAXRECORDS, recordIndex = index % MAXRECORDS;

	return (Record *) &(blockData[blockIndex][RECORD(recordIndex)]);
}

static int partition(char * const blockData[], const int lo, const int hi, const int fieldNo)
{	
	Record * pivot = getRecord(blockData, hi), * recordJ, * recordI;
	
    int i = lo - 1;
	for (int j = lo; j <= hi - 1; j++)
	{
		recordJ = getRecord(blockData, j);
		if (compareRecord(recordJ, pivot, fieldNo))
		{
			recordI = getRecord(blockData, ++i);
			swapRecord(recordI, recordJ);
		}
	}

	recordJ = getRecord(blockData, hi);
	recordI = getRecord(blockData, i + 1);
	if (compareRecord(recordJ, recordI, fieldNo))
		swapRecord(recordJ, recordI);

	return i + 1;
}

static void quickSort(char * const blockData[], const int lo, const int hi, const int fieldNo)
{
	if (lo < hi)
	{
		int piv = partition(blockData, lo, hi, fieldNo);

		quickSort(blockData, lo    , piv - 1, fieldNo);
		quickSort(blockData, piv + 1, hi    , fieldNo);
	}
}

typedef struct mergeBlock{
	int endCounter;		//counter of last block in this team
	int blockCounter;  	//counter of block inside file
	int iterator;		//records iterator inside block (where we write/read)
	BF_Block *block;	//current block
	char *data;			//data of current block
}mergeBlock;

static int initMergeArray(int fileDesc, mergeBlock *blockArray, int bufferSize, int startIndex, int maxBlocks) {

	int allBlocks;
	CALL_OR_EXIT(BF_GetBlockCounter(fileDesc, &allBlocks));

	//Initialization of array
	int index = startIndex;
	int i;
	for (i = 0; i < bufferSize - 1; i++) {
		BF_Block *block;
		BF_Block_Init(&block);
		CALL_OR_EXIT(BF_GetBlock(fileDesc, index, block));
		blockArray[i].data = BF_Block_GetData(block);
		blockArray[i].block = block;
		blockArray[i].iterator = 0;
		blockArray[i].blockCounter = index;
		if (index + maxBlocks > allBlocks)
			blockArray[i].endCounter = allBlocks;
		else
			blockArray[i].endCounter = index + maxBlocks;
		index += maxBlocks;
	}

	return SR_OK;
}

static int getNewBlock(int fileDesc, mergeBlock *blockArray, int minIndex, int *done) {
	//if we went through whole block get a new one
	if (blockArray[minIndex].iterator == *(int *)&blockArray[minIndex].data[RECORDS]) {
		//If there are more blocks to go through in this index
		if (blockArray[minIndex].blockCounter < blockArray[minIndex].endCounter ) {
			CALL_OR_EXIT( BF_UnpinBlock(blockArray[minIndex].block) );
			int index = blockArray[minIndex].blockCounter + 1;
			CALL_OR_EXIT(BF_GetBlock(fileDesc, index, blockArray[minIndex].block));
			blockArray[minIndex].data = BF_Block_GetData(blockArray[minIndex].block);
			blockArray[minIndex].iterator = 0;
			blockArray[minIndex].blockCounter++;
		}
		//else "delete" mergeblock
		else {
			CALL_OR_EXIT( BF_UnpinBlock(blockArray[minIndex].block) );
			BF_Block_Destroy(&blockArray[minIndex].block);
			blockArray[minIndex].iterator = -1;
			blockArray[minIndex].blockCounter = -1;
			blockArray[minIndex].data = NULL;
			(*done)++;
		}
  }
  return SR_OK;
}

static int getNewResultBlock(int newfileDesc, mergeBlock *result) {
	//if result block filled write it and get a new one
	if ((int)result->data[RECORDS] == MAXRECORDS) {
		BF_Block_SetDirty(result->block);
		CALL_OR_EXIT(BF_UnpinBlock(result->block));
		BF_Block_Destroy(&(result->block));

		BF_Block_Init(&(result->block));
		CALL_OR_EXIT(BF_AllocateBlock(newfileDesc, result->block));
		result->data = BF_Block_GetData(result->block);
		result->iterator = 0;
	}
  return SR_OK;
}

static int findMin(mergeBlock *blockArray, int bufferSize, int fieldNo) {
	int minIndex = 0;
	for (int i = 1; i < bufferSize - 1; i++) {
		int it = blockArray[i].iterator;
		int minit = blockArray[minIndex].iterator;
		if (minit == -1) minIndex = i;
		if (blockArray[i].iterator == -1) continue;
		if (compareRecord((Record *)&blockArray[i].data[RECORD(it)], (Record *)&blockArray[minIndex].data[RECORD(minit)], fieldNo) ) {
			minIndex = i;
		}
	}
	return minIndex;
}

static int murgemgurge(int fileDesc, int bufferSize, int startIndex, int maxBlocks ,int fieldNo) {

	SR_CreateFile("racFileMerge");
	int newfileDesc;
	SR_OpenFile("racFileMerge", &newfileDesc);

	mergeBlock *blockArray = malloc( (bufferSize - 1) * sizeof(mergeBlock));

	initMergeArray(fileDesc, blockArray, bufferSize, startIndex, maxBlocks);	

	//Initialization of result block
	mergeBlock result;
	
	BF_Block_Init(&result.block);
	CALL_OR_EXIT(BF_AllocateBlock(newfileDesc, result.block));
	result.data = BF_Block_GetData(result.block);
	result.iterator = 0;
	result.blockCounter = 0; //This does not matter here
	result.endCounter = 0; //This does not matter here
	int zero = 0;
	memcpy((int *)&result.data[RECORDS], &zero, sizeof(int));

	//This holds the number of block "teams" that are finished
	int done = 0;
	while(done != bufferSize - 1) {

		int minIndex = findMin(blockArray, bufferSize, fieldNo);

		int lastit = result.iterator;
		int minit = blockArray[minIndex].iterator;
		
		//write the min record to result block
		memcpy(&result.data[RECORD(lastit)], &blockArray[minIndex].data[RECORD(minit)] , sizeof(Record));

		//Increase iterators for writing/reading
		result.iterator++;
		blockArray[minIndex].iterator++;

		//increase the number of records in result block
		int records = (int)result.data[RECORDS];
		records++;
		memcpy((int *)&result.data[RECORDS], &records, sizeof(int));

		//Check if we went through whole block
		getNewBlock(fileDesc, blockArray, minIndex, &done);
		//Check if result block is filled
		getNewResultBlock(newfileDesc, &result);

	}

	return newfileDesc;
}

SR_ErrorCode SR_SortedFile(
	const char* input_filename,
	const char* output_filename,
	int fieldNo,
	int bufferSize)
{

	for (int i = 0; i < bufferSize; i++) {
		//quicksort
	}

	int tempfd;
	SR_OpenFile(input_filename, &tempfd);

	int fd = murgemgurge(tempfd, bufferSize, 1, bufferSize, fieldNo);

	SR_PrintAllEntries(fd);


	return SR_OK;
}



// Utility Function:
// Returns the given value's length as a string
static int padding(int val)
{
	int length = 0;

	while ( val && ++length ) val /= 10;

	return length;
}

SR_ErrorCode SR_PrintAllEntries(int fileDesc)
{
	if (!isSorted(fileDesc))
		return SR_BF_ERROR;
	
	int blocks;
	CALL_OR_EXIT(BF_GetBlockCounter(fileDesc, &blocks));

	printf("\n\n");
	printf("+-----------+---------------+--------------------+--------------------+\n");
	printf("|ID         |NAME           |SURNAME             |CITY                |\n");
	printf("+-----------+---------------+--------------------+--------------------+\n");

	for (int i = 1; i < blocks; i++)
	{
		BF_Block * block;
		BF_Block_Init(&block);

		CALL_OR_EXIT(BF_GetBlock(fileDesc, i, block));

		char * data = BF_Block_GetData(block);
		for (int j = 0; j < (int) data[RECORDS]; j++)
		{
			Record * record = (Record *) &data[RECORD(j)];
			int whitespace;

			printf("|%d", record->id);
			whitespace = 11 - padding(record->id);
			for (int k = 0; k < whitespace; k++) printf(" ");

			printf("|%s", record->name);
			whitespace = 15 - strlen(record->name);
			for (int k = 0; k < whitespace; k++) printf(" ");

			printf("|%s", record->surname);
			whitespace = 20 - strlen(record->surname);
			for (int k = 0; k < whitespace; k++) printf(" ");

			printf("|%s", record->city);
			whitespace = 20 - strlen(record->city);
			for (int k = 0; k < whitespace; k++) printf(" ");

			printf("|\n+-----------+---------------+--------------------+--------------------+\n");
		}

		CALL_OR_EXIT(BF_UnpinBlock(block));

		BF_Block_Destroy(&block);
	}

	return SR_OK;
}
