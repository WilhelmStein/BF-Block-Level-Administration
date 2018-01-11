#include "sort_file.h"
#include "bf.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define BF_CALL_OR_EXIT(call)	\
{                           	\
	BF_ErrorCode code = call; 	\
	if(code != BF_OK) {       	\
		BF_PrintError(code);    \
		return SR_BF_ERROR;		\
	}                         	\
}

#define SR_CALL_OR_EXIT(call)	\
{								\
	SR_ErrorCode code  = call;	\
	if( code != SR_OK)			\
	{							\
		return code; 			\
	}							\
}								\

// Utility Function:
// Assumes the file has already been opened
// Accesses the file's metadata block
// Checks if its identifier corresponds to that of a sorted file
static bool isSorted(const int fileDesc)
{
	BF_Block * block;
	BF_Block_Init(&block);

	BF_CALL_OR_EXIT(BF_GetBlock(fileDesc, META, block));
	char * blockData = BF_Block_GetData(block);

	bool rv = (blockData[IDENTIFIER] == SORTED);
	BF_CALL_OR_EXIT(BF_UnpinBlock(block));

	BF_Block_Destroy(&block);

	return rv;
}

SR_ErrorCode SR_Init() 
{
  	return SR_OK;
}

SR_ErrorCode SR_CreateFile(const char *fileName) 
{
	BF_CALL_OR_EXIT(BF_CreateFile(fileName));

	int fileDesc;
	BF_Block *block;
	BF_Block_Init(&block);

	BF_CALL_OR_EXIT(BF_OpenFile(fileName, &fileDesc));
	BF_CALL_OR_EXIT(BF_AllocateBlock(fileDesc, block));
	char *data = BF_Block_GetData(block);

	// Set the first byte of first block (metaBlock) to the character 's' 
	data[IDENTIFIER] = SORTED;

	BF_Block_SetDirty(block);
	BF_CALL_OR_EXIT(BF_UnpinBlock(block));
	BF_Block_Destroy(&block);
	BF_CALL_OR_EXIT(BF_CloseFile(fileDesc));

  	return SR_OK;
}

SR_ErrorCode SR_OpenFile(const char *fileName, int *fileDesc)
{
	BF_CALL_OR_EXIT(BF_OpenFile(fileName, fileDesc));
	if (!isSorted(*fileDesc))
	{
		BF_CALL_OR_EXIT(BF_Close(fileName, fileDesc));
		return SR_UNSORTED;
	}

	return SR_OK;
}

SR_ErrorCode SR_CloseFile(int fileDesc)
{
	if (!isSorted(fileDesc))
		return SR_UNSORTED;

	BF_CALL_OR_EXIT(BF_CloseFile(fileDesc));

	return SR_OK;
}

SR_ErrorCode SR_InsertEntry(int fileDesc,	Record record) 
{
	if (!isSorted(fileDesc)) 
		return SR_UNSORTED;

	BF_Block *block;
	BF_Block_Init(&block);

	int blocksNum;
	BF_CALL_OR_EXIT(BF_GetBlockCounter(fileDesc, &blocksNum));
	BF_CALL_OR_EXIT(BF_GetBlock(fileDesc, blocksNum - 1, block));
	char *data = BF_Block_GetData(block);

	// If file has only one black (the metaBlock) OR block is full get a new one and write
	if(blocksNum == 1 || (int)data[RECORDS] == MAXRECORDS) {
		BF_Block *newBlock;
		BF_Block_Init(&newBlock);
		BF_CALL_OR_EXIT(BF_AllocateBlock(fileDesc, newBlock));
		data = BF_Block_GetData(newBlock);

		int one = 1;
		memcpy((int *)&data[RECORDS], &one, sizeof(int));

		memcpy((Record *)&data[RECORD(0)], &record, sizeof(Record));

		BF_Block_SetDirty(newBlock);
		BF_CALL_OR_EXIT(BF_UnpinBlock(newBlock));
		BF_Block_Destroy(&newBlock);
	}
	// Else just write
	else if (blocksNum != 1) {

		int records = (int)data[RECORDS];
		
		// data[RECORD(records)] is the offset where the last record written stops. So write there
		memcpy((Record *)&data[RECORD(records)], &record, sizeof(Record));

		records += 1;
		memcpy((int *)&data[RECORDS], &records, sizeof(int));

		BF_Block_SetDirty(block);
	}

	BF_CALL_OR_EXIT( BF_UnpinBlock(block) );
	BF_Block_Destroy(&block);

  	return SR_OK;
}

// Utility Function:
// Used in comparing two records ("ra" and "rb")
// according to a field specified by fieldNo
// Returns true if "ra" is "lesser" than "rb"
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

// Utility Function:
// Used in swapping two records
// Record "ra" and "rb" should not be overlapping
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

// Utility Function:
// Used in converting one dimensional indexing to two dimensional
// and accessing the record corresponding to the specified index
static Record * getRecord(char * const blockData[], const int index)
{
	int blockIndex = index / MAXRECORDS, recordIndex = index % MAXRECORDS;

	return (Record *) &(blockData[blockIndex][RECORD(recordIndex)]);
}

// Utility Function:
// Treat the chunk of blocks as an one dimensional array
// and partition it based on the "Lomuto partition scheme"
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

// Utility Function:
// Used by "external sort" at "Phase 0"
// in sorting the original chunks of blocks
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
	int endCounter;		  // Counter of first block of the next team
	int blockCounter;  	// Counter of block inside file
	int iterator;		    // Records iterator inside block (where we write/read)
	BF_Block *block;	  // Current block
	char *data;			    // Data of current block
}mergeBlock;

static SR_ErrorCode initMergeArray(int fileDesc, mergeBlock *blockArray, int bufferSize, int startIndex, int maxBlocks) {

	int allBlocks;
	BF_CALL_OR_EXIT(BF_GetBlockCounter(fileDesc, &allBlocks));

	// Initialization of array

	// StartIndex is the first block in all block "teams"
	// Then, index increases by size of team (maxBlocks) so we start from the next team
	int index = startIndex;
	int i;
	for (i = 0; i < bufferSize - 1; i++) {
		// If current index (team's first block) is greater than all the blocks initialize to "invalid" 
		if (index >= allBlocks) {
			blockArray[i].iterator = -1;
			blockArray[i].blockCounter = -1;
			blockArray[i].data = NULL;
			blockArray[i].block = NULL;
			continue;
		}
		// Else get first block of team for each team
		BF_Block *block;
		BF_Block_Init(&block);
		BF_CALL_OR_EXIT(BF_GetBlock(fileDesc, index, block));
		blockArray[i].data = BF_Block_GetData(block);
		blockArray[i].block = block;
		blockArray[i].iterator = 0;
		blockArray[i].blockCounter = index;

		// This check is only for the LAST team of blocks
		// Which MAY have less than maxBlocks
		// If so set the end counter to the end of last block which is allBlocks
		if (index + maxBlocks >= allBlocks)
			blockArray[i].endCounter = allBlocks;
		// Else set it to the start of next team
		else
			blockArray[i].endCounter = index + maxBlocks;
		index += maxBlocks;
	}

	return SR_OK;
}

static SR_ErrorCode getNewBlock(int fileDesc, mergeBlock *blockArray, int minIndex) {
	// If we went through whole block get a new one
	if (blockArray[minIndex].iterator >= *(int *)&blockArray[minIndex].data[RECORDS]) {
		// If there are more blocks to go through in this index
		if (blockArray[minIndex].blockCounter < blockArray[minIndex].endCounter - 1) {
			BF_CALL_OR_EXIT( BF_UnpinBlock(blockArray[minIndex].block) );
			int index = blockArray[minIndex].blockCounter + 1;
			BF_CALL_OR_EXIT(BF_GetBlock(fileDesc, index, blockArray[minIndex].block));
			blockArray[minIndex].data = BF_Block_GetData(blockArray[minIndex].block);
			blockArray[minIndex].iterator = 0;
			blockArray[minIndex].blockCounter++;
		}
		// Else "delete" mergeblock, initialize it to "invalid"
		else {
			BF_CALL_OR_EXIT( BF_UnpinBlock(blockArray[minIndex].block) );
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
	// If result block filled write it and get a new one
	if ((int)result->data[RECORDS] >= MAXRECORDS) {
		BF_Block_SetDirty(result->block);
		BF_CALL_OR_EXIT(BF_UnpinBlock(result->block));
		BF_Block_Destroy(&(result->block));

		// Get a new one
		BF_Block_Init(&(result->block));
		BF_CALL_OR_EXIT(BF_AllocateBlock(newfileDesc, result->block));
		result->data = BF_Block_GetData(result->block);
		result->iterator = 0;
		// Initliaze its records to 0
		int zero = 0;
		memcpy((int *)&result->data[RECORDS], &zero, sizeof(int));
	}
  	return SR_OK;
}

static int findMin(mergeBlock *blockArray, int bufferSize, int fieldNo) {
	int minIndex = 0;

	// Find first index which is valid
	while (minIndex < bufferSize - 1 && blockArray[minIndex].iterator == -1) {
		minIndex++;
	}

	// Start from there
	for (int i = minIndex; i < bufferSize - 1; i++) {

		int it = blockArray[i].iterator;
		int minit = blockArray[minIndex].iterator;

		// If invalid index
		if (it == -1) continue;

		if (compareRecord((Record *)&blockArray[i].data[RECORD(it)], (Record *)&blockArray[minIndex].data[RECORD(minit)], fieldNo) ) {
			minIndex = i;
		}
	}
	// If minIndex >= bufferSize means that there are not valid indices.
	return (minIndex >= bufferSize - 1 ? -1 : minIndex);
}

static SR_ErrorCode Merge(int fileDesc, int newfileDesc, int bufferSize, int startIndex, int maxBlocks ,int fieldNo) {


	mergeBlock *blockArray = malloc( (bufferSize - 1) * sizeof(mergeBlock));

	SR_CALL_OR_EXIT( initMergeArray(fileDesc, blockArray, bufferSize, startIndex, maxBlocks) );

	// Initialization of result block
	mergeBlock result;
	
	BF_Block_Init(&result.block);
	BF_CALL_OR_EXIT(BF_AllocateBlock(newfileDesc, result.block));
	result.data = BF_Block_GetData(result.block);
	result.iterator = 0;
	result.blockCounter = 0; // This does not matter here
	result.endCounter = 0; // This does not matter here
	int zero = 0;
	memcpy((int *)&result.data[RECORDS], &zero, sizeof(int));

	// This holds the number of block "teams" that are finished
	int minIndex;
	// if minIndex == -1 there are no more valid blocks in array so finish up
	while( (minIndex = findMin(blockArray, bufferSize, fieldNo)) != -1 ) {

		// Check if result block is filled
		SR_CALL_OR_EXIT( getNewResultBlock(newfileDesc, &result) );

		int lastit = result.iterator;
		int minit = blockArray[minIndex].iterator;
		
		// Write the min record to result block
		memcpy(&result.data[RECORD(lastit)], &blockArray[minIndex].data[RECORD(minit)] , sizeof(Record));

		// Increase iterators for writing/reading
		result.iterator++;
		blockArray[minIndex].iterator++;

		// Increase the number of records in result block
		int records = (int)result.data[RECORDS];
		records++;
		memcpy((int *)&result.data[RECORDS], &records, sizeof(int));

		// Check if we went through whole block
		SR_CALL_OR_EXIT( getNewBlock(fileDesc, blockArray, minIndex) );
	}

	// Write last result block
	BF_Block_SetDirty(result.block);
	BF_CALL_OR_EXIT(BF_UnpinBlock(result.block));
	BF_Block_Destroy(&result.block);

	return SR_OK;
}

static SR_ErrorCode PhaseZero(int inputfd, int tempQuickfd, int bufferSize, int fieldNo) {
	int allBlocks;
	BF_CALL_OR_EXIT(BF_GetBlockCounter(inputfd, &allBlocks));

	// 2 arrays, one for blocks, one for data in those blocks
	// Indices in one array correspond to the other
	char **blockData = malloc(bufferSize * sizeof(char *));
	int startIndex = 1;

	BF_Block **blockArray = malloc(bufferSize * sizeof(BF_Block *));

	int allRecords;

	// Loop until all teams of bufferSize blocks have been sorted
	while(startIndex < allBlocks) {
		allRecords = 0;

		// Each index in array has one block's data
		// Array has bufferSize indices
		for (int i = 0; i < bufferSize; i++) {
			blockArray[i] = NULL;
			if (startIndex >= allBlocks) {
				break;
			}

			BF_Block_Init(&(blockArray[i]));
			BF_CALL_OR_EXIT(BF_GetBlock(inputfd, startIndex, blockArray[i]));

			blockData[i] = BF_Block_GetData(blockArray[i]);
			allRecords += (int)blockData[i][RECORDS];

			startIndex++;
		}

		// Sort these bufferSize blocks
		quickSort(blockData, 0, allRecords - 1, fieldNo);

		// Write the sorted data into the new file
		for (int i = 0; i < bufferSize; i++) {
			if (!blockArray[i]) break;

			BF_Block *newBlock;
			BF_Block_Init(&newBlock);
			
			BF_CALL_OR_EXIT(BF_AllocateBlock(tempQuickfd, newBlock));
			char *data = BF_Block_GetData(newBlock);

			// Since we have a whole block, we can memcpy all the data into new block
			memcpy(data, blockData[i], BF_BLOCK_SIZE);

			BF_Block_SetDirty(newBlock);
			BF_CALL_OR_EXIT(BF_UnpinBlock(newBlock));
			BF_Block_Destroy(&newBlock);

			BF_CALL_OR_EXIT(BF_UnpinBlock(blockArray[i]));
			BF_Block_Destroy(&(blockArray[i]));
		}

		// Loop until all teams of bufferSize blocks have been sorted
	}

	free(blockArray);
	free(blockData);
	return SR_OK;
}

SR_ErrorCode SR_SortedFile(
	const char* input_filename,
	const char* output_filename,
	int fieldNo,
	int bufferSize)
{
	char * tempFileNames[] = { "tempA.db", "tempB.db" };
	int tempFileFds[] = { -1, -1 };
	
	// Create tempA
	SR_CALL_OR_EXIT( SR_CreateFile(tempFileNames[0]) );
	SR_CALL_OR_EXIT( SR_OpenFile(tempFileNames[0], &tempFileFds[0]) );

	int inputfd;
	SR_CALL_OR_EXIT( SR_OpenFile(input_filename, &inputfd) );

  // Initiate Phase 0 (quickSort) from input file to tempA
	SR_CALL_OR_EXIT( PhaseZero(inputfd, tempFileFds[0], bufferSize, fieldNo) );

	SR_CALL_OR_EXIT( SR_CloseFile(inputfd) );
	
  // Create tempB
	SR_CALL_OR_EXIT( SR_CreateFile(tempFileNames[1]) );
	SR_CALL_OR_EXIT( SR_OpenFile(tempFileNames[1], &tempFileFds[1]) );

	// Index here refers to the tempFileFds array
	// At Start tempFileFds[0] = tempA, tempFileFds[1] = tempB
	int index = 1;
	int maxBlocks = bufferSize;
	int allBlocks;
	BF_CALL_OR_EXIT(BF_GetBlockCounter(tempFileFds[0], &allBlocks));


	// Phase One - n
	do {
		
		for (int i = 1; i < allBlocks - 1; i += (maxBlocks * (bufferSize - 1)) ) {
			// i is the first blockCounter from all the bufferSize - 1 teams that are to be merged
			// Take !index as input, and index as output, and merge
			// Merge bufferSize - 1 teams of blocks
			Merge(tempFileFds[!index], tempFileFds[index], bufferSize, i, maxBlocks, fieldNo);
			// i now gets increased by the size of bufferSize - 1 teams
			// So next i will be the first block of the next bufferSize - 1 teams that are to be merged
		}
		
    // Close and remove the input file
		SR_CALL_OR_EXIT( SR_CloseFile(tempFileFds[!index]) );
		remove(tempFileNames[!index]);
		
    // Create a new one
		SR_CALL_OR_EXIT( SR_CreateFile(tempFileNames[!index]) );
		SR_CALL_OR_EXIT( SR_OpenFile(tempFileNames[!index], &tempFileFds[!index]) );


		// Switch indices
		index = !index;
		// Now the new file we created above will be the new output file
		// And the old file which was output will now be input

		maxBlocks *= (bufferSize - 1);

		// maxBlocks is the max size that a "team" of blocks can have
		// if maxBlocks is more than all the blocks in file stop merging
	} while(maxBlocks < allBlocks - 1);

  // Change the last outputfile to output_fileName
	SR_CALL_OR_EXIT( SR_CloseFile(tempFileFds[!index]) );

	rename(tempFileNames[!index], output_filename);
	
	SR_CALL_OR_EXIT( SR_CloseFile(tempFileFds[index]) );
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
	BF_CALL_OR_EXIT(BF_GetBlockCounter(fileDesc, &blocks));

	printf("\n\n");
	printf("+-----------+---------------+--------------------+--------------------+\n");
	printf("|ID         |NAME           |SURNAME             |CITY                |\n");
	printf("+-----------+---------------+--------------------+--------------------+\n");

	int kostas = 0;
	for (int i = 1; i < blocks; i++)
	{
		BF_Block * block;
		BF_Block_Init(&block);

		BF_CALL_OR_EXIT(BF_GetBlock(fileDesc, i, block));

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

		BF_CALL_OR_EXIT(BF_UnpinBlock(block));

		BF_Block_Destroy(&block);
	}
	setvbuf (stdout, NULL, _IONBF, 0);
	printf("\nrecords : %d", kostas);
	return SR_OK;
}
