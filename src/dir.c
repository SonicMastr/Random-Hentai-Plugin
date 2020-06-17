#include "dir.h"
#include "common.h"
#include "util.h"

SceIoDirenter *head = NULL;
static int length = 0;

static char *getFile(int number) {
	printf("number %i\n", number);
	SceIoDirenter *currenter = head;
	printf("name 1 %s\n", currenter->d_name);
	for (int i = 0; i < number - 1; i++) {
		currenter = currenter->next;
	}
	printf("name 2 %s\n", currenter->d_name);
	return currenter->d_name;
}

char *getRandomImage() {
	int min = 0;
	SceUInt randomNumber;
	int result;
	if (!length)
		return NULL;
	sceKernelGetRandomNumber(&randomNumber, 4);
	result = (randomNumber % (length-min+1)) + min;
	return getFile(result);
}

void readDir() {
	sceIoMkdir("ux0:data/randomhentai/saved", 0777);
	head = (SceIoDirenter *)alloc(sizeof(SceIoDirenter));
	SceIoDirenter * current = head;

	int dfd;
	dfd = sceIoDopen("ux0:data/randomhentai/saved/");
	if(dfd >= 0) { 
		int res = 1;
		while(res > 0) {
			SceIoDirent dir;
			res = sceIoDread(dfd, &dir);
			if(res > 0) {
				sceClibMemcpy(current->d_name, dir.d_name, strlen(dir.d_name)+1);
				current->next = NULL;
				current->next = (SceIoDirenter *)alloc(sizeof(SceIoDirenter));
				current = current->next;
				length = length + 1;
			}
		}
		sceIoDclose(dfd);
		sceClibPrintf("File count: %d\n", length);
	}
}