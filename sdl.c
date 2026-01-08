// sdl.c - Simple Downloader v0.1
// A command-line utility to download files from a given URL with a progress bar.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// main function - entry point of the program
int main(int argc, char *argv[]) {
    // Variable to store the URL provided by the user
    char *url = NULL;

    // Check if enough arguments are provided
    if (argc < 3) {
        fprintf(stderr, "Usage: %s --url <URL> or -u <URL>\n", argv[0]);
        return EXIT_FAILURE; // Exit with an error code
    }

    // Loop through command-line arguments to find the URL
    for (int i = 1; i < argc; i++) {
        // Check for --url argument
        if (strcmp(argv[i], "--url") == 0 || strcmp(argv[i], "-u") == 0) {
            // Ensure there is a URL provided after --url or -u
            if (i + 1 < argc) {
                url = argv[i + 1]; // Store the URL
                i++; // Skip the next argument as it's the URL
            } else {
                fprintf(stderr, "Error: No URL provided after %s\n", argv[i]);
                return EXIT_FAILURE;
            }
        }
    }

    // Check if a URL was successfully extracted
    if (url == NULL) {
        fprintf(stderr, "Error: URL argument (--url or -u) not found.\n");
        return EXIT_FAILURE;
    }

    // For now, just print the URL to confirm it's parsed correctly
    printf("URL to download: %s\n", url);

    // Placeholder for download logic
    printf("Download functionality will be implemented here.\n");

    return EXIT_SUCCESS; // Exit successfully
}
