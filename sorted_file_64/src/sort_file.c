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

static int partition(const char *  const blockData[], const int beg, const int end)
{	
	const int last = (* (int *) blockData[RECORDS]) - 1;
	Record * pivot = (Record *) &blockData[RECORD(last)];


}

static void qsort(const char *  const blockData[], const int beg, const int end)
{
	if (beg < end)
	{
		int piv = partition(blockData, end, beg);
		qsort(blockData, beg, piv - 1);
		qsort(blockData, piv + 1, end);
	}
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
	SR_OpenFile("tempFile", &tempfd);


	return SR_OK;
}

typedef struct mergeBlock{
	int endCounter;		//counter of last block in this team
	int blockCounter;  	//counter of block inside file
	int iterator;		//records iterator inside block (where we write/read)
	BF_Block *block;	//current block
	char *data;			//data of current block
}mergeBlock;

static int murgemgurge(int fileDesc, int bufferSize, int startIndex, int maxBlocks ,int fieldNo) {

	SR_CreateFile("tempFileMerge");
	int newfileDesc;
	SR_OpenFile("tempFileMerge", &newfileDesc);

	mergeBlock *blockArray = malloc( (bufferSize - 1) * sizeof(mergeBlock));

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

	//Initialization of result block
	BF_Block *block;
	BF_Block_Init(&block);
	CALL_OR_EXIT(BF_AllocateBlock(newfileDesc, block));
	mergeBlock result;
	result.data = BF_Block_GetData(block);
	result.iterator = 0;
	result.block = block;
	result.blockCounter = 0; //This does not matter here
	blockArray[i].endCounter = 0; //This does not matter here
	int zero = 0;
	memcpy((int *)&result.data[RECORDS], &zero, sizeof(int));

	//it will not be while true ...
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

		//if we went through whole block get a new one
		if (blockArray[minIndex].iterator == *(int *)&blockArray[minIndex].data[RECORDS]) {
			//If there are more blocks to go through in this index
			if (blockArray[minIndex].blockCounter < blockArray[minIndex].endCounter ) {
				CALL_OR_EXIT( BF_UnpinBlock(blockArray[minIndex].block) );
				int index = blockArray[minIndex].blockCounter + 1;
				CALL_OR_EXIT(BF_GetBlock(fileDesc, index, blockArray[minIndex].block));
				blockArray[minIndex].block = block;
				blockArray[minIndex].data = BF_Block_GetData(block);
				blockArray[minIndex].iterator = 0;
				blockArray[minIndex].blockCounter++;
			}
			//else "delete" mergeblock
			else {
				CALL_OR_EXIT( BF_UnpinBlock(blockArray[minIndex].block) );
				BF_Block_Destroy(blockArray[minIndex].block);
				blockArray[minIndex].iterator = -1;
				blockArray[minIndex].blockCounter = -1;
				blockArray[minIndex].data = NULL;
				done++;
			}
		}

		//if result block filled write it and get a new one
		if (result.data[RECORDS] == MAXRECORDS) {
			BF_Block_SetDirty(result.block);
			CALL_OR_EXIT(BF_UnpinBlock(result.block));
			CALL_OR_EXIT(BF_AllocateBlock(newfileDesc, &result.block));
			result.data = BF_Block_GetData(result.block);
			result.iterator = 0;
		}

	}

}

static int findMin(mergeBlock *blockArray, int bufferSize, int fieldNo) {
	int minIndex = 0;
	for (int i = 1; i <= bufferSize - 1; i++) {
		int it = blockArray[i].iterator;
		int minit = blockArray[minIndex].iterator;
		if (blockArray[i].iterator == -1) continue;
		if (compareRecord((Record *)blockArray[i].data[RECORD(it)], (Record *)blockArray[minIndex].data[RECORD(minit)], fieldNo) ) {
			minIndex = i;
		}
	}
	return minIndex;
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
