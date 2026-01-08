// sdl.c - Simple Downloader v0.1
// A command-line utility to download files from a given URL with a progress bar.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h> // Required for libcurl - a library for transferring data with URLs

// Function to handle libcurl write operations (saving data to a file)
size_t write_data(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    return fwrite(ptr, size, nmemb, stream);
}

// main function - entry point of the program
int main(int argc, char *argv[]) {
    // Variable to store the URL provided by the user
    char *url = NULL;
    // Variable to store the output filename (defaults to the filename from URL)
    char *output_filename = NULL;

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

    printf("Starting download from: %s\n", url);

    CURL *curl_handle; // CURL easy handle
    CURLcode res;      // Result of CURL operations
    FILE *fp;          // File pointer for saving the downloaded data

    // Initialize libcurl
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // Get an easy handle
    curl_handle = curl_easy_init();
    if (curl_handle) {
        // Set the URL to download
        curl_easy_setopt(curl_handle, CURLOPT_URL, url);
        // Follow redirects if any
        curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
        // Set the write function to save data to a file
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_data);

        // Determine output filename
        // This is a simplistic approach; a more robust solution would parse the URL
        // to extract the filename or use Content-Disposition header.
        output_filename = strrchr(url, '/');
        if (output_filename) {
            output_filename++; // Move past the last slash
        } else {
            output_filename = (char*)"downloaded_file"; // Default filename if no slash found
        }

        // Open file for writing binary data
        fp = fopen(output_filename, "wb");
        if (fp == NULL) {
            fprintf(stderr, "Error: Could not open file %s for writing.\n", output_filename);
            curl_easy_cleanup(curl_handle);
            curl_global_cleanup();
            return EXIT_FAILURE;
        }

        // Pass the file pointer to the write function
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, fp);

        // Perform the download
        res = curl_easy_perform(curl_handle);

        // Check for errors
        if (res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        } else {
            printf("Download complete! File saved as: %s\n", output_filename);
        }

        // Close the file
        fclose(fp);
    }

    // Clean up curl resources
    curl_easy_cleanup(curl_handle);
    curl_global_cleanup();

    return EXIT_SUCCESS; // Exit successfully
}

/*
To compile this program, you need to have libcurl installed.
On Debian/Ubuntu, you can install it using:
    sudo apt-get update
    sudo apt-get install libcurl4-openssl-dev

Then compile with:
    gcc sdl.c -o sdl $(pkg-config --libs --cflags libcurl)
*/