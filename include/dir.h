#ifndef DIR_H
#define DIR_H

typedef struct SceIoDirenter {
     char d_name[256];  
     struct SceIoDirenter * next;   
    } SceIoDirenter;


char *getRandomImage(void);
void readDir(void);

#endif