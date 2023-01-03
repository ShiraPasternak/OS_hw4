//
// Created by shira on 01/01/2023.
//

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h> //for PATH_MAX
#include <linux/threads.h> //for threads
#include <unistd.h>

bool rootDirIsSearchable(char *dir);

void handleDirIsNotSearchable(char *dir);

int oneThreadHasFailed = 0;
int successfulSearchesCounter = 0;

int main(int argc, char **argv) {
    char *rootDir, *searchTerm;
    int numOfThreads;

    if (argc != 4) {
        fprintf(stderr,"%s","incorrect number of inputs\n");
        exit(1);
    } else if(!rootDirIsSearchable(argv[1])){
        handleDirIsNotSearchable(argv[1]);
    } else {
        rootDir = argv[1];
        searchTerm = argv[2];
        sscanf(argv[3], "%d", &numOfThreads);
    }



    printf("Done searching, found %d files\n", successfulSearchesCounter);
    if(oneThreadHasFailed)
        exit(1);
    else
        exit(0);
}

void handleDirIsNotSearchable(char *dir) {
    printf("Directory %s: Permission denied.\n", dir);
}

bool rootDirIsSearchable(char *dir) {
    if (access(dir, R_OK && X_OK) == 0) {
        return true;
    }
    return false;
}
