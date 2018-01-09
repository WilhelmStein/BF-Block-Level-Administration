#include "sort_file.h"
#include "bf.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>


#define CALL_OR_EXIT(call)        	\
{                                	\
	BF_ErrorCode code = call;		\
	if (code != BF_OK) {			\
		BF_PrintError(code);		\
		exit(SR_ERROR);				\
	}								\
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


SR_ErrorCode SR_OpenFile(const char *fileName, int *fileDesc) {
  	// Your code goes here

  	return SR_OK;
}


SR_ErrorCode SR_CloseFile(int fileDesc) {
  	// Your code goes here

  	return SR_OK;
}


SR_ErrorCode SR_InsertEntry(int fileDesc,	Record record) {
  	// Your code goes here
	int full = 0;

	BF_Block *block;
	BF_Block_Init(&block);

	int blocksNum;
	CALL_OR_EXIT(BF_GetBlockCounter(fileDesc, &blocksNum));

	char *data = BF_Block_GetData(block);

	
	if(blocksNum == 1 || data[RECORDS] == MAXRECORDS) {
		BF_Block *newBlock;
		BF_Block_Init(&newBlock);
		CALL_OR_EXIT(BF_AllocateBlock(fileDesc, newBlock));
		data = BF_Block_GetData(newBlock);

		int one = 1;
		memcpy(data[RECORDS], &one, 4);

		memcpy(data[RECORD(0)], &record, sizeof(Record));

		printf("id : %d\n", data[RECORD(0) + 0]);
		printf("name : %s\n", data[RECORD(0) + 4]);
		printf("surname : %s\n", data[RECORD(0) + 4 + 15]);
		printf("city : %s\n", data[RECORD(0) + 4 + 15 + 20]);

		BF_Block_SetDirty(newBlock);
		BF_UnpinBlock(newBlock);
		BF_Block_Destroy(&newBlock);
	}
	else if (blocksNum != 1) {
		CALL_OR_EXIT(BF_GetBlock(fileDesc, blocksNum - 1, block));

		int records = data[RECORDS];
		
		memcpy(data[RECORD(records)], &record, sizeof(Record));

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
	int bufferSize
) {
	// Your code goes here

	return SR_OK;
}


SR_ErrorCode SR_PrintAllEntries(int fileDesc) {
	// Your code goes here

	return SR_OK;
}
