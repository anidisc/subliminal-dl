
#define SDL_VERSION "0.2.1"

// sdl.c - Simple Downloader v0.1
// A command-line utility to download files from a given URL with a progress bar.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h> // Required for libcurl - a library for transferring data with URLs
#include <sys/time.h> // Required for gettimeofday to calculate download speed
#include <unistd.h>   // Required for access() to check file existence

// --- Color definitions for progress bar ---
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_RESET   "\x1b[0m"

// Global variable to hold the total number of downloads for progress bar rendering
static int g_total_downloads = 0;
// Global flag to always overwrite files if they exist
static int g_always_overwrite = 0;

// Structure to hold progress bar data, including data for speed calculation
struct progress_data {
    curl_off_t last_dl_now;    // Last reported downloaded bytes
    long long last_time_us; // Last time in microseconds
};

// Structure to hold all context for a single transfer
struct transfer_context {
    CURL *easy_handle;          // The easy handle for this specific transfer
    FILE *fp;                   // The file pointer for the output file
    char *filename;             // The name of the output file
    struct progress_data progress; // The progress data for this transfer's progress bar
    int line_number;            // The terminal line number for this transfer's progress bar
};

// Function to handle libcurl write operations (saving data to a file)
size_t write_data(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    return fwrite(ptr, size, nmemb, stream);
}

// Helper function to generate a new filename for a copy
char* generate_copy_filename(const char* original_filename) {
    char* new_filename = NULL;
    char* basename = strdup(original_filename);
    char* dot = strrchr(basename, '.');
    if (dot) {
        *dot = '\0'; // Temporarily terminate basename at the dot
    }
    const char* extension = dot ? dot + 1 : "";

    int i = 1;
    while (1) {
        // Allocate space for "basename(i).extension\0"
        new_filename = malloc(strlen(basename) + 10 + strlen(extension) + 1);
        if (dot) {
            sprintf(new_filename, "%s(%d).%s", basename, i, extension);
        } else {
            sprintf(new_filename, "%s(%d)", basename, i);
        }

        if (access(new_filename, F_OK) != 0) {
            // File does not exist, we found a unique name
            break;
        }
        free(new_filename);
        i++;
    }
    
    free(basename);
    return new_filename;
}

// Function to display the progress bar (updated for CURLOPT_XFERINFOFUNCTION)
int progress_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    struct transfer_context *context = (struct transfer_context *)clientp;
    struct progress_data *progress = &context->progress;
    (void)ultotal; // ultotal is unused for download progress
    (void)ulnow;   // ulnow is unused for download progress

    // Static variable to hold the calculated speed, so it persists across calls
    static double displayed_speed_bps = 0.0;

    // Get current time in microseconds
    struct timeval tv;
    gettimeofday(&tv, NULL);
    long long current_time_us = (long long)tv.tv_sec * 1000000 + tv.tv_usec;

    // --- Speed Calculation ---
    double time_diff_sec = (double)(current_time_us - progress->last_time_us) / 1000000.0;
    
    if (time_diff_sec > 0.5) {
        curl_off_t data_diff = dlnow - progress->last_dl_now;
        displayed_speed_bps = (double)data_diff / time_diff_sec; // bytes per second
        progress->last_dl_now = dlnow;
        progress->last_time_us = current_time_us;
    } else if (progress->last_time_us == 0) {
        progress->last_dl_now = dlnow;
        progress->last_time_us = current_time_us;
    }


    // --- Progress Bar Display ---
    if (dltotal > 0) {
        int progress_bar_width = 40; // Smaller width for multi-download
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

        // --- ANSI Cursor Manipulation for multi-bar display ---
        // Move cursor up to the correct line, print, then move back down
        printf("\r\x1b[%dA", g_total_downloads - context->line_number);
        
        // Print the filename and progress bar
        printf("%-20.20s [", context->filename);
        for (int i = 0; i < num_blocks; i++) {
            printf(ANSI_COLOR_GREEN "\u2588" ANSI_COLOR_RESET);
        }
        for (int i = num_blocks; i < progress_bar_width; i++) {
            printf("\u2591");
        }
        printf("] %.2f%% %-12s", progress_percentage, speed_str);

        // Move cursor back down to the line below the progress bars
        printf("\x1b[%dB", g_total_downloads - context->line_number);
        fflush(stdout); // Flush the output buffer to display immediately
    }

    return 0; // Return 0 to continue the transfer
}

// main function - entry point of the program
int main(int argc, char *argv[]) {
    // Variable to store the URL provided by the user
    char *url = NULL;
    // Array to store multiple URLs for multi-download mode
    char *urls[100]; // Max 100 URLs for now
    int num_urls = 0;
    int multi_mode = 0; // Flag for multi-download mode

    // Check if enough arguments are provided
    if (argc < 2) {
        fprintf(stderr, "Usage: %s [--always-overwrite | -aw] [--url <URL> | --multi <URL1> ...]\n", argv[0]);
        fprintf(stderr, "       %s --version\n", argv[0]);
        return EXIT_FAILURE; // Exit with an error code
    }

    // Loop through command-line arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
            printf("sdl version %s\n", SDL_VERSION);
            return EXIT_SUCCESS;
        } else if (strcmp(argv[i], "--always-overwrite") == 0 || strcmp(argv[i], "-aw") == 0) {
            g_always_overwrite = 1;
        } else if (strcmp(argv[i], "--url") == 0 || strcmp(argv[i], "-u") == 0) {
            if (++i < argc) {
                url = argv[i];
                num_urls = 1;
            } else {
                fprintf(stderr, "Error: No URL provided after %s\n", argv[i]);
                return EXIT_FAILURE;
            }
        } else if (strcmp(argv[i], "--multi") == 0 || strcmp(argv[i], "-m") == 0) {
            multi_mode = 1;
            // Collect all subsequent arguments as URLs
            while (++i < argc && argv[i][0] != '-') {
                if (num_urls < 100) {
                    urls[num_urls++] = argv[i];
                }
            }
            i--; // Decrement because the loop will increment it
            
            if (num_urls == 0) {
                fprintf(stderr, "Error: No URLs provided after %s\n", argv[i-1]);
                return EXIT_FAILURE;
            }
        }
    }

    // Check if any URL was provided
    if (num_urls == 0 && url == NULL) {
        fprintf(stderr, "Error: No URL(s) provided. Use --url or --multi.\n");
        return EXIT_FAILURE;
    }

    // If not in multi_mode, but a single url was provided
    if (!multi_mode && url != NULL) {
        urls[0] = url;
    }

    g_total_downloads = num_urls; // Set global for progress callback

    // --- Multi-download logic setup ---
    CURLM *multi_handle = NULL;
    struct transfer_context contexts[100]; // Array to hold contexts for each download

    // Initialize libcurl global state
    curl_global_init(CURL_GLOBAL_DEFAULT);
    multi_handle = curl_multi_init();

    if (!multi_handle) {
        fprintf(stderr, "Error: curl_multi_init() failed.\n");
        return EXIT_FAILURE;
    }
    
    // Prepare each transfer
    for (int i = 0; i < num_urls; i++) {
        // Initialize context fields to NULL/0
        memset(&contexts[i], 0, sizeof(struct transfer_context));

        contexts[i].easy_handle = curl_easy_init();
        if (!contexts[i].easy_handle) {
            fprintf(stderr, "Error: curl_easy_init() failed for URL %s\n", urls[i]);
            continue;
        }

        // Determine output filename from URL
        char* original_filename_ptr = strrchr(urls[i], '/');
        if (original_filename_ptr) {
            contexts[i].filename = strdup(original_filename_ptr + 1);
        } else {
            contexts[i].filename = strdup("downloaded_file"); 
        }

        // --- File conflict check ---
        if (!g_always_overwrite && access(contexts[i].filename, F_OK) == 0) {
            printf("File '%s' already exists. [O]verwrite, [C]opy, [S]kip? ", contexts[i].filename);
            int choice = getchar();
            while (getchar() != '\n' && choice != '\n'); // Clear stdin buffer

            switch (choice) {
                case 'c':
                case 'C': {
                    char* new_name = generate_copy_filename(contexts[i].filename);
                    free(contexts[i].filename); // Free the old name
                    contexts[i].filename = new_name;
                    printf("Will save as '%s'\n", contexts[i].filename);
                    break;
                }
                case 's':
                case 'S':
                    printf("Skipping download for '%s'\n", urls[i]);
                    free(contexts[i].filename);
                    contexts[i].filename = NULL;
                    curl_easy_cleanup(contexts[i].easy_handle);
                    contexts[i].easy_handle = NULL; // Mark as skipped
                    continue; // Go to next URL in the loop
                case 'o':
                case 'O':
                default:
                    printf("Overwriting '%s'\n", contexts[i].filename);
                    break; // Default is to overwrite
            }
        }

        // Open file for writing
        contexts[i].fp = fopen(contexts[i].filename, "wb");
        if (!contexts[i].fp) {
            fprintf(stderr, "Error: Could not open file %s for writing.\n", contexts[i].filename);
            free(contexts[i].filename);
            contexts[i].filename = NULL;
            curl_easy_cleanup(contexts[i].easy_handle);
            contexts[i].easy_handle = NULL; // Mark as failed/skipped
            continue;
        }
        
        // Set curl options for this handle
        curl_easy_setopt(contexts[i].easy_handle, CURLOPT_URL, urls[i]);
        curl_easy_setopt(contexts[i].easy_handle, CURLOPT_WRITEFUNCTION, write_data);
        curl_easy_setopt(contexts[i].easy_handle, CURLOPT_WRITEDATA, contexts[i].fp);
        curl_easy_setopt(contexts[i].easy_handle, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(contexts[i].easy_handle, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(contexts[i].easy_handle, CURLOPT_XFERINFOFUNCTION, progress_callback);
        
        // Pass the context for this transfer to the progress callback
        curl_easy_setopt(contexts[i].easy_handle, CURLOPT_XFERINFODATA, &contexts[i]);
        
        // Add the easy handle to the multi handle
        curl_multi_add_handle(multi_handle, contexts[i].easy_handle);
    }
    
    // --- Recalculate totals and assign line numbers for active downloads ---
    int active_downloads = 0;
    for (int i = 0; i < num_urls; i++) {
        if (contexts[i].easy_handle) {
            contexts[i].line_number = active_downloads++;
        }
    }
    g_total_downloads = active_downloads;

    // Print empty lines to make space for progress bars
    for (int i = 0; i < g_total_downloads; i++) {
        printf("\n");
    }

    // --- Perform the transfers ---
    int still_running = 0;
    curl_multi_perform(multi_handle, &still_running);

    do {
        int numfds;
        int res = curl_multi_wait(multi_handle, NULL, 0, 1000, &numfds);
        if (res != CURLM_OK) {
            fprintf(stderr, "Error: curl_multi_wait() failed.\n");
            break;
        }

        curl_multi_perform(multi_handle, &still_running);

        // Check for finished transfers
        CURLMsg *msg;
        int msgs_left;
        while ((msg = curl_multi_info_read(multi_handle, &msgs_left))) {
            if (msg->msg == CURLMSG_DONE) {
                // Find which transfer this message belongs to
                for (int i = 0; i < num_urls; i++) {
                    if (contexts[i].easy_handle == msg->easy_handle) {
                        // Optional: Print final status for this transfer
                        // The progress bar already shows 100%, so this might be redundant
                        break;
                    }
                }
            }
        }
    } while (still_running);
    

    // --- Cleanup ---
    for (int i = 0; i < num_urls; i++) {
        if (contexts[i].easy_handle) {
            curl_multi_remove_handle(multi_handle, contexts[i].easy_handle);
            curl_easy_cleanup(contexts[i].easy_handle);
        }
        if (contexts[i].fp) fclose(contexts[i].fp);
        if (contexts[i].filename) free(contexts[i].filename); // Always free filename
    }
    curl_multi_cleanup(multi_handle);
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
