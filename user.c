#include <stdio.h>
#include <string.h>

#include "common.h"
 
int main() {
	FILE *fvfs;
	FILE *fptregs;
	FILE *fargs;
	int input;
	
	printf("Enter 0 to print VFSMOUNT structure, 1 to print PT_REGS structure\n");
	printf("> ");
	scanf("%d", &input);

	char *line = NULL;
	size_t len = 0;

	if (input == 0) {
		fargs = fopen(DEBUGFS_ARGS_PATH, "w");
		if (fargs == NULL) {
			printf("VFSmount args file can't be opened\n");
			return 1;
		}

		printf("Enter FD:\n");
		printf("> ");
		scanf("%d", &input);

		char inbuf[256];
		sprintf(inbuf, "%d", input);
		fwrite(&inbuf, 1, sizeof(inbuf), fargs);
		fclose(fargs);

		printf("VFSMOUNT structure:\n");

		fvfs = fopen(DEBUGFS_VFS_PATH, "r");
		if (fvfs == NULL) {
			printf("VFSmount struct file can't be opened\n");
			return 1;
		}
		while (getline(&line, &len, fvfs) != -1) {
			printf("%s", line);
		}	

		fclose(fvfs);

	} else if (input == 1) {
		fptregs = fopen(DEBUGFS_PTREGS_PATH, "r");
		if (fptregs == NULL) {
			printf("PT_regs files can't be opened\n");
			return 2;
		}

		printf("PT_regs strucrure: \n");
		while (getline(&line, &len, fptregs) != -1) {
			printf("%s", line);
		}
		fclose(fptregs);

	} else {
		printf("Input is invalid\n");
	}

    	
	
    	return 0;
}






