
#define SDL_VERSION "0.3.0"

// sdl.c - Simple Downloader
// A command-line utility to download files from a given URL with a progress
// bar.

#include <ctype.h>
#include <curl/curl.h> // Required for libcurl - a library for transferring data with URLs
#include <errno.h> // Required for errno and EEXIST
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h> // Required for stat() and mkdir()
#include <sys/time.h> // Required for gettimeofday to calculate download speed
#include <unistd.h>   // Required for access() to check file existence

// --- Color definitions for progress bar ---
#define ANSI_COLOR_GREEN "\x1b[32m"
#define ANSI_COLOR_RED "\x1b[31m" // Define ANSI color for red
#define ANSI_COLOR_RESET "\x1b[0m"

// Global variable to hold the total number of downloads for progress bar
// rendering
static int g_total_downloads = 0;
// Global flag to always overwrite files if they exist
static int g_always_overwrite = 0;
// Global variable for the destination directory
static char *g_destination_dir = NULL;

// Structure to hold progress bar data, including data for speed calculation
struct progress_data {
  curl_off_t last_dl_now; // Last reported downloaded bytes
  long long last_time_us; // Last time in microseconds
};

// Enum to represent the status of a download
enum transfer_status {
  STATUS_PENDING,
  STATUS_DOWNLOADING,
  STATUS_FAILED,
  STATUS_SUCCESS
};

// Structure to hold all context for a single transfer
struct transfer_context {
  CURL *easy_handle; // The easy handle for this specific transfer
  FILE *fp;          // The file pointer for the output file
  char *filename;    // The name of the output file
  struct progress_data
      progress;    // The progress data for this transfer's progress bar
  int line_number; // The terminal line number for this transfer's progress bar
  enum transfer_status status; // The current status of the download
  long response_code;          // To store the final HTTP response code
};

// Function to handle libcurl write operations (saving data to a file)
size_t write_data(void *ptr, size_t size, size_t nmemb, FILE *stream) {
  return fwrite(ptr, size, nmemb, stream);
}

// Helper function to create a directory path recursively
int mkdir_recursive(const char *path, mode_t mode) {
  char *p;
  char *path_copy = strdup(path);

  for (p = path_copy + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0';
      if (mkdir(path_copy, mode) != 0) {
        if (errno != EEXIST) {
          free(path_copy);
          return -1;
        }
      }
      *p = '/';
    }
  }
  if (mkdir(path_copy, mode) != 0) {
    if (errno != EEXIST) {
      free(path_copy);
      return -1;
    }
  }

  free(path_copy);
  return 0;
}

// Helper function to generate a new filename for a copy
char *generate_copy_filename(const char *original_filename) {
  char *new_filename = NULL;
  char *basename = strdup(original_filename);
  char *dot = strrchr(basename, '.');
  if (dot) {
    *dot = '\0'; // Temporarily terminate basename at the dot
  }
  const char *extension = dot ? dot + 1 : "";

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
int progress_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                      curl_off_t ultotal, curl_off_t ulnow) {
  struct transfer_context *context = (struct transfer_context *)clientp;
  (void)ultotal;
  (void)ulnow; // Unused for downloads

  // --- State Machine Logic ---
  if (context->status == STATUS_PENDING) {
    curl_easy_getinfo(context->easy_handle, CURLINFO_RESPONSE_CODE,
                      &context->response_code);
    if (context->response_code > 0) { // Headers received
      if (context->response_code >= 200 && context->response_code < 300) {
        context->status = STATUS_DOWNLOADING;
      } else if (context->response_code >= 300 &&
                 context->response_code < 400) {
        // Ignore 3xx codes (redirects) - libcurl handles them with
        // CURLOPT_FOLLOWLOCATION We stay in STATUS_PENDING until the final 200
        // OK is received
      } else {
        context->status = STATUS_FAILED;
      }
    }
  }

  if (context->status == STATUS_PENDING) {
    return 0; // Don't draw anything until headers are received
  }

  // --- ANSI Cursor Manipulation for multi-bar display ---
  // Save cursor position and move to the correct line
  printf("\r\x1b[%dA", g_total_downloads - context->line_number);

  // Find the base filename for display
  const char *display_filename = strrchr(context->filename, '/');
  display_filename =
      display_filename ? display_filename + 1 : context->filename;

  int bar_width = 40;

  switch (context->status) {
  case STATUS_DOWNLOADING: {
    if (dltotal > 0) {
      // Speed calculation
      struct progress_data *progress = &context->progress;
      static double speed_bps = 0.0;
      struct timeval tv;
      gettimeofday(&tv, NULL);
      long long current_time_us = (long long)tv.tv_sec * 1000000 + tv.tv_usec;
      double time_diff_sec =
          (double)(current_time_us - progress->last_time_us) / 1000000.0;

      if (time_diff_sec > 0.5) {
        speed_bps = (double)(dlnow - progress->last_dl_now) / time_diff_sec;
        progress->last_dl_now = dlnow;
        progress->last_time_us = current_time_us;
      } else if (progress->last_time_us == 0) {
        progress->last_dl_now = dlnow;
        progress->last_time_us = current_time_us;
      }

      // Format speed for display
      char speed_str[32];
      if (speed_bps < 1024)
        snprintf(speed_str, sizeof(speed_str), "%.0f B/s", speed_bps);
      else if (speed_bps < 1024 * 1024)
        snprintf(speed_str, sizeof(speed_str), "%.1f KB/s", speed_bps / 1024);
      else
        snprintf(speed_str, sizeof(speed_str), "%.1f MB/s",
                 speed_bps / (1024 * 1024));

      // Draw the progress bar
      double percentage = ((double)dlnow / dltotal) * 100.0;
      int num_blocks = (int)(percentage / 100.0 * bar_width);

      char colored_bracket[32]; // Buffer for colored bracket
      snprintf(colored_bracket, sizeof(colored_bracket), "[%s",
               ANSI_COLOR_GREEN);
      printf("%-20.20s %s", display_filename, colored_bracket);

      for (int i = 0; i < num_blocks; i++)
        printf("\u2588");
      printf(ANSI_COLOR_RESET);
      for (int i = num_blocks; i < bar_width; i++)
        printf("\u2591");
      printf("] %6.2f%% %-12s", percentage, speed_str);
    }
    break;
  }
  case STATUS_FAILED: {
    // Draw a "Failed" message
    char colored_bracket[32]; // Buffer for colored bracket
    snprintf(colored_bracket, sizeof(colored_bracket), "[%s", ANSI_COLOR_RED);
    printf("%-20.20s %s", display_filename, colored_bracket);

    for (int i = 0; i < bar_width; i++)
      printf("!");
    printf(ANSI_COLOR_RESET "] Failed (Code: %ld)       ",
           context->response_code);
    break;
  }
  default:
    // Should not happen during transfer
    break;
  }

  // Move cursor back down to the line below the progress bars
  printf("\x1b[%dB", g_total_downloads - context->line_number);
  fflush(stdout); // Flush the output buffer to display immediately

  return 0; // Return 0 to continue the transfer
}

// main function - entry point of the program
int main(int argc, char *argv[]) {
  // Variable to store the URL provided by the user
  char *url = NULL;
  // Array to store multiple URLs for multi-download mode
  char *urls[1000]; // Max 1000 URLs
  int num_urls = 0;
  int multi_mode = 0; // Flag for multi-download mode

  // Check if enough arguments are provided
  if (argc < 2) {
    fprintf(stderr,
            "Usage: %s [options] [--url <URL> | --multi <URL1> ... | --file "
            "<file>]\n",
            argv[0]);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -f, --file <file>          Read URLs from a file\n");
    fprintf(stderr, "  -d, --destination <dir>    Set destination directory\n");
    fprintf(stderr,
            "  -aw, --always-overwrite    Always overwrite existing files\n");
    fprintf(stderr, "  -v, --version              Show version\n");
    return EXIT_FAILURE; // Exit with an error code
  }

  // Loop through command-line arguments
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
      printf("sdl version %s\n", SDL_VERSION);
      return EXIT_SUCCESS;
    } else if (strcmp(argv[i], "--always-overwrite") == 0 ||
               strcmp(argv[i], "-aw") == 0) {
      g_always_overwrite = 1;
    } else if (strcmp(argv[i], "--destination") == 0 ||
               strcmp(argv[i], "-d") == 0) {
      if (++i < argc) {
        g_destination_dir = argv[i];
      } else {
        fprintf(stderr, "Error: No directory provided after %s\n", argv[i - 1]);
        return EXIT_FAILURE;
      }
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
        if (num_urls < 1000) {
          urls[num_urls++] = argv[i];
        }
      }
      i--; // Decrement because the loop will increment it

      if (num_urls == 0) {
        fprintf(stderr, "Error: No URLs provided after %s\n", argv[i - 1]);
        return EXIT_FAILURE;
      }
    } else if (strcmp(argv[i], "--file") == 0 || strcmp(argv[i], "-f") == 0) {
      if (++i < argc) {
        FILE *file = fopen(argv[i], "r");
        if (!file) {
          fprintf(stderr, "Error: Could not open file '%s'\n", argv[i]);
          return EXIT_FAILURE;
        }

        char line[2048];
        while (fgets(line, sizeof(line), file)) {
          // Trim whitespace
          char *p = line;
          while (isspace((unsigned char)*p))
            p++; // Trim leading
          if (*p == 0)
            continue; // Empty line

          char *end = p + strlen(p) - 1;
          while (end > p && isspace((unsigned char)*end))
            end--; // Trim trailing
          *(end + 1) = 0;

          if (num_urls < 1000) {
            urls[num_urls++] = strdup(p);
          } else {
            fprintf(stderr, "Warning: Maximum number of URLs (1000) reached. "
                            "Ignoring remaining.\n");
            break;
          }
        }
        fclose(file);
        multi_mode = 1; // Treat file input as multi-mode
      } else {
        fprintf(stderr, "Error: No file provided after %s\n", argv[i - 1]);
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

  // --- Handle destination directory ---
  if (g_destination_dir) {
    struct stat st = {0};
    if (stat(g_destination_dir, &st) == -1) {
      // Directory does not exist, ask to create it
      printf("Destination directory '%s' does not exist. Create it? [Y/n] ",
             g_destination_dir);
      int choice = getchar();
      while (getchar() != '\n' && choice != '\n')
        ; // Clear stdin

      if (choice == 'y' || choice == 'Y' || choice == '\n') {
        if (mkdir_recursive(g_destination_dir, 0755) == -1) {
          fprintf(stderr, "Error: Could not create directory '%s'.\n",
                  g_destination_dir);
          return EXIT_FAILURE;
        }
        printf("Directory '%s' created.\n", g_destination_dir);
      } else {
        fprintf(stderr, "Aborted. Destination directory does not exist.\n");
        return EXIT_FAILURE;
      }
    } else if (!S_ISDIR(st.st_mode)) {
      fprintf(stderr,
              "Error: Destination path '%s' exists but is not a directory.\n",
              g_destination_dir);
      return EXIT_FAILURE;
    }
  }

  // --- Multi-download logic setup ---
  CURLM *multi_handle = NULL;
  struct transfer_context
      contexts[100]; // Array to hold contexts for each download

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
    contexts[i].status = STATUS_PENDING;

    contexts[i].easy_handle = curl_easy_init();
    if (!contexts[i].easy_handle) {
      fprintf(stderr, "Error: curl_easy_init() failed for URL %s\n", urls[i]);
      continue;
    }

    // Determine base filename from URL
    char *base_filename_ptr = strrchr(urls[i], '/');
    if (!base_filename_ptr) {
      base_filename_ptr = "downloaded_file";
    } else {
      base_filename_ptr++; // Move past the slash
    }

    // Construct full path if destination dir is set
    if (g_destination_dir) {
      size_t path_len = strlen(g_destination_dir) + strlen(base_filename_ptr) +
                        2; // +2 for '/' and '\0'
      contexts[i].filename = malloc(path_len);
      snprintf(contexts[i].filename, path_len, "%s/%s", g_destination_dir,
               base_filename_ptr);
    } else {
      contexts[i].filename = strdup(base_filename_ptr);
    }

    // --- File conflict check ---
    if (!g_always_overwrite && access(contexts[i].filename, F_OK) == 0) {
      printf("File '%s' already exists. [O]verwrite, [C]opy, [S]kip? ",
             contexts[i].filename);
      int choice = getchar();
      while (getchar() != '\n' && choice != '\n')
        ; // Clear stdin buffer

      switch (choice) {
      case 'c':
      case 'C': {
        char *new_name = generate_copy_filename(contexts[i].filename);
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
        continue;                       // Go to next URL in the loop
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
      fprintf(stderr, "Error: Could not open file %s for writing.\n",
              contexts[i].filename);
      free(contexts[i].filename);
      contexts[i].filename = NULL;
      curl_easy_cleanup(contexts[i].easy_handle);
      contexts[i].easy_handle = NULL; // Mark as failed/skipped
      continue;
    }

    // Set curl options for this handle
    curl_easy_setopt(contexts[i].easy_handle, CURLOPT_URL, urls[i]);
    curl_easy_setopt(contexts[i].easy_handle, CURLOPT_WRITEFUNCTION,
                     write_data);
    curl_easy_setopt(contexts[i].easy_handle, CURLOPT_WRITEDATA,
                     contexts[i].fp);
    curl_easy_setopt(contexts[i].easy_handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(contexts[i].easy_handle, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(contexts[i].easy_handle, CURLOPT_XFERINFOFUNCTION,
                     progress_callback);

    // Pass the context for this transfer to the progress callback
    curl_easy_setopt(contexts[i].easy_handle, CURLOPT_XFERINFODATA,
                     &contexts[i]);

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
        CURL *easy_handle = msg->easy_handle;
        CURLcode result = msg->data.result;

        // Find which transfer this message belongs to
        for (int i = 0; i < num_urls; i++) {
          if (contexts[i].easy_handle == easy_handle) {
            long response_code = 0;
            curl_easy_getinfo(easy_handle, CURLINFO_RESPONSE_CODE,
                              &response_code);
            contexts[i].response_code = response_code;

            if (result == CURLE_OK &&
                (response_code >= 200 && response_code < 300)) {
              contexts[i].status = STATUS_SUCCESS;
            } else {
              contexts[i].status = STATUS_FAILED;
              if (contexts[i].fp) {
                fclose(contexts[i].fp);
                contexts[i].fp = NULL; // Avoid double close
                remove(contexts[i].filename);
              }
            }
            break;
          }
        }
      }
    }
  } while (still_running);

  // --- Final Rendering Pass ---
  // Iterate through all contexts and print their final status on their lines
  for (int i = 0; i < num_urls; i++) {
    if (contexts[i].easy_handle == NULL)
      continue; // Skip if this handle was never added or was skipped

    // Move cursor up to the correct line
    printf("\r\x1b[%dA", g_total_downloads - contexts[i].line_number);

    // Find the base filename for display
    const char *display_filename = strrchr(contexts[i].filename, '/');
    display_filename =
        display_filename ? display_filename + 1 : contexts[i].filename;

    int bar_width = 40; // Same as in progress_callback

    switch (contexts[i].status) {
    case STATUS_SUCCESS:
      printf("%-20.20s ", display_filename); // Print filename and space
      printf("[%s", ANSI_COLOR_GREEN);       // Print colored opening bracket
      for (int k = 0; k < bar_width; k++)
        printf("\u2588");
      printf(ANSI_COLOR_RESET
             "] [Completed]               "); // Clear the rest of the line
      break;
    case STATUS_FAILED:
      printf("%-20.20s ", display_filename); // Print filename and space
      printf("[%s", ANSI_COLOR_RED);         // Print colored opening bracket
      for (int k = 0; k < bar_width; k++)
        printf("!");
      printf(ANSI_COLOR_RESET "] [Failed (Code: %ld)]",
             contexts[i].response_code);
      break;
    case STATUS_PENDING:     // Should not happen for active transfers, but for
                             // robustness
    case STATUS_DOWNLOADING: // Should now be finished, if not SUCCESS or FAILED
    default:
      printf("%-20.20s [----------------------------------------] "
             "[Skipped/Error]       ",
             display_filename);
      break;
    }
    // Move cursor back down
    printf("\x1b[%dB", g_total_downloads - contexts[i].line_number);
  }
  printf("\n"); // Final newline to push the prompt below the output

  // --- Cleanup ---
  for (int i = 0; i < num_urls; i++) {
    if (contexts[i].easy_handle) {
      curl_multi_remove_handle(multi_handle, contexts[i].easy_handle);
      curl_easy_cleanup(contexts[i].easy_handle);
    }
    if (contexts[i].fp)
      fclose(contexts[i].fp);
    if (contexts[i].filename)
      free(contexts[i].filename); // Always free filename
  }
  curl_multi_cleanup(multi_handle);
  curl_global_cleanup();

  // Print final newlines to clear the progress bars and push the prompt down
  for (int i = 0; i < g_total_downloads; i++) {
    printf("\n");
  }

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
