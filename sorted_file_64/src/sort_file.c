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

SR_ErrorCode SR_Init() {
  	// Your code goes here

  	return SR_OK;
}


SR_ErrorCode SR_CreateFile(const char *fileName) {
  	// Your code goes here

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


SR_ErrorCode SR_InsertEntry(int fileDesc,	Record record) {
  	// Your code goes here
	int full = 0;

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

		printf("id : %d\n", (int)data[RECORD(0) + 0]);
		printf("name : %s\n", (char *)&data[RECORD(0) + 4]);
		printf("surname : %s\n", (char *)&data[RECORD(0) + 4 + 15]);
		printf("city : %s\n", (char *)&data[RECORD(0) + 4 + 15 + 20]);

		BF_Block_SetDirty(newBlock);
		BF_UnpinBlock(newBlock);
		BF_Block_Destroy(&newBlock);
	}
	else if (blocksNum != 1) {

		int records = (int)data[RECORDS];
		
		memcpy((Record *)&data[RECORD(records)], &record, sizeof(Record));

		printf("id : %d\n", (int)data[RECORD(records) + 0]);
		printf("name : %s\n", (char *)&data[RECORD(records) + 4]);
		printf("surname : %s\n", (char *)&data[RECORD(records) + 4 + 15]);
		printf("city : %s\n", (char *)&data[RECORD(records) + 4 + 15 + 20]);

		records += 1;
		memcpy((int *)&data[RECORDS], &records, sizeof(int));

		BF_Block_SetDirty(block);
	}

	BF_UnpinBlock(block);
	BF_Block_Destroy(&block);

  	return SR_OK;
}


SR_ErrorCode SR_SortedFile(
	const char* input_filename,
	const char* output_filename,
	int fieldNo,
	int bufferSize)
{
	// Your code goes here

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

	printf("+-----------+-----------+---------------+--------------------+--------------------+\n");
	printf("|INDEX      |ID         |NAME           |SURNAME             |CITY                |\n");
	printf("+-----------+-----------+---------------+--------------------+--------------------+\n");

	for (int i = 0; i < blocks; i++)
	{
		BF_Block * block;
		BF_Block_Init(&block);

		CALL_OR_EXIT(BF_GetBlock(fileDesc, i, block));

		char * data = BF_Block_GetData(block);
		for (int i = 0; i < (int) data[RECORDS]; i++)
		{
			Record * record = (Record *) data[RECORD(i)];
			int whitespace;

			printf("|%d", (i + 1));
			whitespace = 11 - padding(i + 1);
			for (int i = 0; i < whitespace; i++) printf(' ');

			printf("|%d", record->id);
			whitespace = 11 - padding(record->id);
			for (int i = 0; i < whitespace; i++) printf(' ');

			printf("|%s", record->name);
			whitespace = 15 - strlen(record->name);
			for (int i = 0; i < whitespace; i++) printf(' ');

			printf("|%s", record->surname);
			whitespace = 20 - strlen(record->surname);
			for (int i = 0; i < whitespace; i++) printf(' ');

			printf("|%s", record->city);
			whitespace = 20 - strlen(record->city);
			for (int i = 0; i < whitespace; i++) printf(' ');

			printf("\n+-----------+-----------+---------------+--------------------+--------------------+\n");
		}

		CALL_OR_EXIT(BF_UnpinBlock(block));

		BF_Block_Destroy(block);
	}

	return SR_OK;
}
