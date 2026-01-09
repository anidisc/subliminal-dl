
// sdl.c - Simple Downloader v0.1
// A command-line utility to download files from a given URL with a progress bar.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h> // Required for libcurl - a library for transferring data with URLs
#include <sys/time.h> // Required for gettimeofday to calculate download speed

// --- Color definitions for progress bar ---
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_RESET   "\x1b[0m"

// Structure to hold progress bar data, including data for speed calculation
struct progress_data {
    curl_off_t last_dl_now;    // Last reported downloaded bytes
    long long last_time_us; // Last time in microseconds
};

// Function to handle libcurl write operations (saving data to a file)
size_t write_data(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    return fwrite(ptr, size, nmemb, stream);
}

// Function to display the progress bar (updated for CURLOPT_XFERINFOFUNCTION)
int progress_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    struct progress_data *progress = (struct progress_data *)clientp;
    (void)ultotal; // ultotal is unused for download progress
    (void)ulnow;   // ulnow is unused for download progress

    // Static variable to hold the calculated speed, so it persists across calls
    static double displayed_speed_bps = 0.0;

    // Get current time in microseconds
    struct timeval tv;
    gettimeofday(&tv, NULL);
    long long current_time_us = (long long)tv.tv_sec * 1000000 + tv.tv_usec;

    // --- Speed Calculation ---
    // We only update the speed calculation every half a second to get a stable reading.
    double time_diff_sec = (double)(current_time_us - progress->last_time_us) / 1000000.0;
    
    if (time_diff_sec > 0.5) {
        curl_off_t data_diff = dlnow - progress->last_dl_now;
        displayed_speed_bps = (double)data_diff / time_diff_sec; // bytes per second

        // Update markers for the next speed calculation
        progress->last_dl_now = dlnow;
        progress->last_time_us = current_time_us;
    } else if (progress->last_time_us == 0) {
        // Handle the very first call, initialize the time and data markers
        progress->last_dl_now = dlnow;
        progress->last_time_us = current_time_us;
    }


    // --- Progress Bar Display ---
    // Only update the progress bar if there's actual download data
    if (dltotal > 0) {
        int progress_bar_width = 50; // Width of the progress bar
        double progress_percentage = ((double)dlnow / dltotal) * 100.0;
        int num_blocks = (int)(progress_percentage / 100.0 * progress_bar_width);

        // Format speed for display
        char speed_str[32];
        if (displayed_speed_bps < 1024) {
            snprintf(speed_str, sizeof(speed_str), "%.0f B/s", displayed_speed_bps);
        } else if (displayed_speed_bps < 1024 * 1024) {
            snprintf(speed_str, sizeof(speed_str), "%.1f KB/s", displayed_speed_bps / 1024.0);
        } else {
            snprintf(speed_str, sizeof(speed_str), "%.1f MB/s", displayed_speed_bps / (1024.0 * 1024.0));
        }
        
        printf("\r["); // \r returns the cursor to the beginning of the line
        for (int i = 0; i < num_blocks; i++) {
            printf(ANSI_COLOR_GREEN "\u2588" ANSI_COLOR_RESET); // Filled block (Unicode full block)
        }
        for (int i = num_blocks; i < progress_bar_width; i++) {
            printf("\u2591"); // Empty block (Unicode light shade block)
        }
        // Add spaces at the end to clear previous, longer speed strings
        printf("] %.2f%% %-12s", progress_percentage, speed_str);
        fflush(stdout); // Flush the output buffer to display immediately
    }

    return 0; // Return 0 to continue the transfer
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

        // Enable progress meter
        curl_easy_setopt(curl_handle, CURLOPT_NOPROGRESS, 0L);
        // Set progress callback function using the new XFERINFOFUNCTION
        struct progress_data progress = {0, 0}; // Initialize all members to zero
        curl_easy_setopt(curl_handle, CURLOPT_XFERINFOFUNCTION, progress_callback);
        // Pass progress data to the callback function (CURLOPT_XFERINFODATA for the new function)
        curl_easy_setopt(curl_handle, CURLOPT_XFERINFODATA, &progress);


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
            fprintf(stderr, "\ncurl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        } else {
            printf("\nDownload complete! File saved as: %s\n", output_filename);
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

On openSUSE, you can install it using:
    sudo zypper install libcurl-devel

Then compile with:
    gcc sdl.c -o sdl $(pkg-config --libs --cflags libcurl)
*/
