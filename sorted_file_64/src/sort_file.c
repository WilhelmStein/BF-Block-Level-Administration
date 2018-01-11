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
	allBlocks--;

	//Initialization of array
	int index = startIndex;
	int i;
	for (i = 0; i < bufferSize - 1; i++) {
		if (index >= allBlocks) {
			blockArray[i].iterator = -1;
			blockArray[i].blockCounter = -1;
			blockArray[i].data = NULL;
			blockArray[i].block = NULL;
			continue;
		}
		BF_Block *block;
		BF_Block_Init(&block);
		CALL_OR_EXIT(BF_GetBlock(fileDesc, index, block));
		blockArray[i].data = BF_Block_GetData(block);
		blockArray[i].block = block;
		blockArray[i].iterator = 0;
		blockArray[i].blockCounter = index;
		if (index + maxBlocks >= allBlocks)
			blockArray[i].endCounter = allBlocks;
		else
			blockArray[i].endCounter = index + maxBlocks;
		index += maxBlocks;
	}

	return SR_OK;
}

static int getNewBlock(int fileDesc, mergeBlock *blockArray, int minIndex) {
	//if we went through whole block get a new one
	if (blockArray[minIndex].iterator >= *(int *)&blockArray[minIndex].data[RECORDS]) {
		//If there are more blocks to go through in this index
		if (blockArray[minIndex].blockCounter < blockArray[minIndex].endCounter - 1) {
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
			blockArray[minIndex].block = NULL;
		}
  	}
 	return SR_OK;
}

static int getNewResultBlock(int newfileDesc, mergeBlock *result) {
	//if result block filled write it and get a new one
	if ((int)result->data[RECORDS] >= MAXRECORDS) {
		BF_Block_SetDirty(result->block);
		CALL_OR_EXIT(BF_UnpinBlock(result->block));
		BF_Block_Destroy(&(result->block));

		BF_Block_Init(&(result->block));
		CALL_OR_EXIT(BF_AllocateBlock(newfileDesc, result->block));
		result->data = BF_Block_GetData(result->block);
		result->iterator = 0;
		int zero = 0;
		memcpy((int *)&result->data[RECORDS], &zero, sizeof(int));
	}
  	return SR_OK;
}

static int findMin(mergeBlock *blockArray, int bufferSize, int fieldNo) {
	int minIndex = 0;

	while (minIndex < bufferSize - 1 && blockArray[minIndex].iterator == -1) {
		minIndex++;
	}

	for (int i = minIndex; i < bufferSize - 1; i++) {

		int it = blockArray[i].iterator;
		int minit = blockArray[minIndex].iterator;

		if (it == -1) continue;

		if (compareRecord((Record *)&blockArray[i].data[RECORD(it)], (Record *)&blockArray[minIndex].data[RECORD(minit)], fieldNo) ) {
			minIndex = i;
		}
	}
	return (minIndex >= bufferSize - 1 ? -1 : minIndex);
}

static SR_ErrorCode murgemgurge(int fileDesc, int newfileDesc, int bufferSize, int startIndex, int maxBlocks ,int fieldNo) {


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
	int minIndex;
	while( (minIndex = findMin(blockArray, bufferSize, fieldNo)) != -1 ) {

		//Check if result block is filled
		getNewResultBlock(newfileDesc, &result);

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
		getNewBlock(fileDesc, blockArray, minIndex);
	}

	BF_Block_SetDirty(result.block);
	CALL_OR_EXIT(BF_UnpinBlock(result.block));
	BF_Block_Destroy(&result.block);

	return SR_OK;
}

static int stepA(int inputfd, int tempQuickfd, int bufferSize, int fieldNo) {
	int allBlocks;
	CALL_OR_EXIT(BF_GetBlockCounter(inputfd, &allBlocks));

	char **blockData = malloc(bufferSize * sizeof(char *));
	int startIndex = 1;

	BF_Block **blockArray = malloc(bufferSize * sizeof(BF_Block *));

	int allRecords;

	while(startIndex < allBlocks) {
		allRecords = 0;

		for (int i = 0; i < bufferSize; i++) {
			blockArray[i] = NULL;
			if (startIndex >= allBlocks) {
				break;
			}

			BF_Block_Init(&(blockArray[i]));
			CALL_OR_EXIT(BF_GetBlock(inputfd, startIndex, blockArray[i]));

			blockData[i] = BF_Block_GetData(blockArray[i]);
			allRecords += (int)blockData[i][RECORDS];

			startIndex++;
		}

		quickSort(blockData, 0, allRecords - 1, fieldNo);

		for (int i = 0; i < bufferSize; i++) {
			if (!blockArray[i]) break;

			BF_Block *newBlock;
			BF_Block_Init(&newBlock);
			
			CALL_OR_EXIT(BF_AllocateBlock(tempQuickfd, newBlock));
			char *data = BF_Block_GetData(newBlock);

			memcpy(data, blockData[i], BF_BLOCK_SIZE);

			BF_Block_SetDirty(newBlock);
			CALL_OR_EXIT(BF_UnpinBlock(newBlock));
			BF_Block_Destroy(&newBlock);

			CALL_OR_EXIT(BF_UnpinBlock(blockArray[i]));
			BF_Block_Destroy(&(blockArray[i]));
		}
	}

	free(blockArray);
	free(blockData);
}

SR_ErrorCode SR_SortedFile(
	const char* input_filename,
	const char* output_filename,
	int fieldNo,
	int bufferSize)
{
	char * tempFileNames[] = { "tempA.db", "tempB.db" };
	int tempFileFds[] = { -1, -1 };
	
	SR_CreateFile(tempFileNames[0]);
	SR_OpenFile(tempFileNames[0], &tempFileFds[0]);

	int inputfd;
	SR_OpenFile(input_filename, &inputfd);

	stepA(inputfd, tempFileFds[0], bufferSize, fieldNo);

	SR_CloseFile(inputfd);
	
	SR_CreateFile(tempFileNames[1]);
	SR_OpenFile(tempFileNames[1], &tempFileFds[1]);
	 

	int index = 1;
	int maxBlocks = bufferSize;
	int allBlocks;
	CALL_OR_EXIT(BF_GetBlockCounter(tempFileFds[0], &allBlocks));

	do {

		for (int i = 1; i < allBlocks - 1; i += (maxBlocks * (bufferSize - 1)) ) {
			SR_PrintAllEntries(tempFileFds[!index]);
			murgemgurge(tempFileFds[!index], tempFileFds[index], bufferSize, i, maxBlocks, fieldNo);
			SR_PrintAllEntries(tempFileFds[!index]);
		}


		SR_CloseFile(tempFileFds[!index]);
		remove(tempFileNames[!index]);
		
		SR_CreateFile(tempFileNames[!index]);
		SR_OpenFile(tempFileNames[!index], &tempFileFds[!index]);

		index = !index;

		maxBlocks *= (bufferSize - 1);

	} while(maxBlocks < allBlocks - 1);

	SR_PrintAllEntries(tempFileFds[!index]);

	SR_CloseFile(tempFileFds[!index]);
	rename(tempFileNames[!index], output_filename);
	
	SR_CloseFile(tempFileFds[index]);
	remove(tempFileNames[index]);
	
	return SR_OK;
}



// Utility Function:
// Returns the given value's length as a string
static int padding(int val)
{
	char IntToStr[12];
	sprintf(IntToStr, "%d", val);

	return strlen(IntToStr);
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

	int kostas = 0;
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
			kostas++;
		}

		

		CALL_OR_EXIT(BF_UnpinBlock(block));

		BF_Block_Destroy(&block);
	}
	setvbuf (stdout, NULL, _IONBF, 0);
	printf("\nrecords : %d", kostas);
	return SR_OK;
}
