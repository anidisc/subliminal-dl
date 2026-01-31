
#define SDL_VERSION "0.54.0"

// sdl.c - Subiliminal Downloader
// A command-line utility to download files from a given URL with a progress
// bar.

#include <ctype.h>
#include <curl/curl.h> // Required for libcurl - a library for transferring data with URLs
#include <errno.h>  // Required for errno and EEXIST
#include <signal.h> // Required for signal handling (e.g., Ctrl+C)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h> // Required for stat() and mkdir()
#include <sys/time.h> // Required for gettimeofday to calculate download speed
#include <unistd.h>   // Required for access() to check file existence

// --- Cursor and Signal Handling ---
void show_cursor() {
  printf("\x1b[?25h");
  fflush(stdout);
}

void handle_sigint(int sig) {
  printf("\n\n"); // Move to a new line clear from progress bars
  show_cursor();
  printf("Download interrupted by user. Exiting.\n");
  exit(130); // Standard exit code for processes terminated by Ctrl+C
}

// --- Gofile.io API specific logic ---

// Global variable to store the Gofile guest token
static char *g_gofile_token = NULL;

// Structure to hold data fetched from curl in memory
struct MemoryStruct {
  char *memory;
  size_t size;
};

// Callback function for curl to write data into a MemoryStruct
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb,
                                  void *userp) {
  size_t realsize = size * nmemb;
  struct MemoryStruct *mem = (struct MemoryStruct *)userp;

  char *ptr = realloc(mem->memory, mem->size + realsize + 1);
  if (ptr == NULL) {
    fprintf(stderr, "Error: not enough memory (realloc returned NULL)\n");
    return 0;
  }

  mem->memory = ptr;
  memcpy(&(mem->memory[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->memory[mem->size] = 0;

  return realsize;
}

// Fetches a guest token from Gofile and stores it in the global variable
void ensure_gofile_token() {
  if (g_gofile_token != NULL) {
    return; // Token already exists
  }

  printf("Fetching Gofile guest token...\n");
  CURL *curl_handle;
  CURLcode res;
  struct MemoryStruct chunk;
  chunk.memory = malloc(1);
  chunk.size = 0;

  curl_handle = curl_easy_init();
  if (curl_handle) {
    // Mimic headers from the python script
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "User-Agent: Mozilla/5.0");
    headers = curl_slist_append(headers, "Accept: */*");
    headers = curl_slist_append(headers, "Accept-Encoding: gzip");
    headers = curl_slist_append(headers, "Connection: keep-alive");

    curl_easy_setopt(curl_handle, CURLOPT_URL,
                     "https://api.gofile.io/accounts");
    curl_easy_setopt(curl_handle, CURLOPT_POST, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, ""); // No data needed
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);

    res = curl_easy_perform(curl_handle);

    if (res == CURLE_OK) {
      long response_code;
      curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &response_code);
      if (response_code >= 200 && response_code < 300) {
        const char *status_key = "\"status\": \"ok\"";
        if (strstr(chunk.memory, status_key)) {
          const char *token_key = "\"token\": \"";
          char *ptr = strstr(chunk.memory, token_key);
          if (ptr) {
            ptr += strlen(token_key);
            char *end_ptr = strchr(ptr, '"');
            if (end_ptr) {
              size_t token_len = end_ptr - ptr;
              g_gofile_token = malloc(token_len + 1);
              memcpy(g_gofile_token, ptr, token_len);
              g_gofile_token[token_len] = '\0';
              printf("Gofile token obtained successfully.\n");
            }
          }
        } else {
          fprintf(stderr, "Failed to get Gofile token: API status not 'ok'.\n");
        }
      }
    } else {
      fprintf(stderr, "Failed to get Gofile token: %s\n",
              curl_easy_strerror(res));
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl_handle);
  }
  free(chunk.memory);
}

// Function to check if a URL is a Gofile URL
int is_gofile_url(const char *url) {
  return (strstr(url, "gofile.io/d/") != NULL);
}

// Resolves Gofile URLs, adding direct links to the list, and passing non-gofile
// URLs through.
void resolve_urls(char **original_urls, int original_num_urls,
                  char **final_urls, int *final_num_urls, const int max_urls) {
  CURL *curl_handle;
  CURLcode res;

  for (int i = 0; i < original_num_urls; i++) {
    if (is_gofile_url(original_urls[i])) {
      printf("Resolving Gofile URL: %s\n", original_urls[i]);

      ensure_gofile_token();
      if (!g_gofile_token) {
        fprintf(stderr, "Skipping Gofile URL, token not available: %s\n",
                original_urls[i]);
        continue;
      }

      const char *content_id_ptr = strrchr(original_urls[i], '/');
      if (!content_id_ptr)
        continue;
      const char *content_id = content_id_ptr + 1;

      char api_url[512];
      snprintf(api_url, sizeof(api_url),
               "https://api.gofile.io/contents/"
               "%s?cache=true&sortField=createTime&sortDirection=1",
               content_id);

      struct MemoryStruct chunk;
      chunk.memory = malloc(1);
      chunk.size = 0;

      curl_handle = curl_easy_init();
      if (curl_handle) {
        char auth_header[512];
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s",
                 g_gofile_token);
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, auth_header);

        curl_easy_setopt(curl_handle, CURLOPT_URL, api_url);
        curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION,
                         WriteMemoryCallback);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);

        res = curl_easy_perform(curl_handle);

        if (res == CURLE_OK) {
          long response_code;
          curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE,
                            &response_code);

          if (response_code >= 200 && response_code < 300) {
            const char *ptr = chunk.memory;
            const char *link_key = "\"link\": \"";
            while ((ptr = strstr(ptr, link_key)) != NULL) {
              ptr += strlen(link_key);
              const char *end_ptr = strchr(ptr, '"');
              if (end_ptr) {
                if (*final_num_urls < max_urls) {
                  size_t link_len = end_ptr - ptr;
                  char *direct_link = malloc(link_len + 1);
                  memcpy(direct_link, ptr, link_len);
                  direct_link[link_len] = '\0';
                  final_urls[*final_num_urls] = direct_link;
                  (*final_num_urls)++;
                  printf("  -> Found direct link: %s\n", direct_link);
                } else {
                  fprintf(stderr, "Warning: Max URLs reached, ignoring further "
                                  "Gofile links.\n");
                  break;
                }
                ptr = end_ptr;
              }
            }
          } else {
            fprintf(stderr, "Gofile API returned HTTP %ld for %s\n",
                    response_code, api_url);
          }
        } else {
          fprintf(stderr, "curl_easy_perform() failed: %s\n",
                  curl_easy_strerror(res));
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl_handle);
      }
      free(chunk.memory);
    } else {
      if (*final_num_urls < max_urls) {
        final_urls[*final_num_urls] = original_urls[i];
        (*final_num_urls)++;
      }
    }
  }
}

// --- Color definitions for progress bar ---
#define ANSI_COLOR_GREEN "\x1b[32m"
#define ANSI_COLOR_RED "\x1b[31m" // Define ANSI color for red
#define ANSI_COLOR_CYAN "\x1b[36m"
#define ANSI_COLOR_RESET "\x1b[0m"

// Global variable to hold the total number of downloads for progress bar
// rendering
static int g_total_downloads = 0;
// Global flag to always overwrite files if they exist
static int g_always_overwrite = 0;
// Global variable for the destination directory
static char *g_destination_dir = NULL;
// Global variable for maximum parallel downloads (default 1000 for "parallel")
static int g_max_parallel = 1000;
// Global flag to disable progress bar rendering, showing only percentage and
// speed
// Global flag for queue mode (process and remove from dburl.txt)
static int g_queue_mode = 0;
static const char *DB_FILENAME = "dburl.txt";

// Global flag to disable progress bar rendering, showing only percentage and
// speed
static int g_no_progress_bar = 0;

// Function to add a URL to the persistent DB
void add_url_to_db(const char *url) {
  FILE *fp = fopen(DB_FILENAME, "a+");
  if (!fp) {
    fprintf(stderr, "Error: Could not open %s for appending.\n", DB_FILENAME);
    exit(EXIT_FAILURE);
  }

  // Check for duplicates
  char line[2048];
  fseek(fp, 0, SEEK_SET); // Start from beginning
  while (fgets(line, sizeof(line), fp)) {
    // Trim newline
    line[strcspn(line, "\n")] = 0;
    if (strcmp(line, url) == 0) {
      printf("URL already exists in queue: %s\n", url);
      fclose(fp);
      return;
    }
  }

  fprintf(fp, "%s\n", url);
  printf("Added to queue: %s\n", url);
  fclose(fp);
}

// Function to remove a URL from the persistent DB
void remove_url_from_db(const char *url_to_remove) {
  FILE *fp = fopen(DB_FILENAME, "r");
  if (!fp)
    return; // File might not exist

  // We'll write to a temp file
  char temp_filename[256];
  snprintf(temp_filename, sizeof(temp_filename), "%s.tmp", DB_FILENAME);
  FILE *temp_fp = fopen(temp_filename, "w");
  if (!temp_fp) {
    fprintf(stderr, "Error: Could not create temp file for DB update.\n");
    fclose(fp);
    return;
  }

  char line[2048];
  int found = 0;
  while (fgets(line, sizeof(line), fp)) {
    char *p = line;
    // Trim newline for comparison
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n')
      line[len - 1] = '\0';

    if (strcmp(line, url_to_remove) != 0) {
      fprintf(temp_fp, "%s\n", line);
    } else {
      found = 1;
    }
  }

  fclose(fp);
  fclose(temp_fp);

  if (found) {
    if (rename(temp_filename, DB_FILENAME) != 0) {
      fprintf(stderr, "Error updating DB file.\n");
    }
  } else {
    remove(temp_filename);
  }
}

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
  CURL *easy_handle;        // The easy handle for this specific transfer
  FILE *fp;                 // The file pointer for the output file
  char *filename;           // The name of the final output file
  char *part_filename;      // The name of the partial file
  curl_off_t resume_offset; // Offset to resume from
  struct progress_data
      progress;    // The progress data for this transfer's progress bar
  int line_number; // The terminal line number for this transfer's progress bar
  enum transfer_status status; // The current status of the download
  long response_code;          // To store the final HTTP response code
  int restart_needed; // Flag to indicate if download needs to be restarted
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
      if (context->response_code == 206) {
        // Resume successful
        context->status = STATUS_DOWNLOADING;
      } else if (context->response_code >= 200 &&
                 context->response_code < 300) {
        if (context->resume_offset > 0) {
          // Server ignored range header, must restart
          context->restart_needed = 1;
          return 1; // Abort transfer
        }
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

      // Draw the progress bar or just percentage/speed
      double percentage = 0.0;
      if (dltotal > 0) {
        percentage = ((double)(context->resume_offset + dlnow) /
                      (context->resume_offset + dltotal)) *
                     100.0;
      }

      if (g_no_progress_bar) {
        // Braille spinner frames
        const char *spinner_frames[] = {"⠋", "⠙", "⠹", "⠸", "⠼",
                                        "⠴", "⠦", "⠧", "⠇", "⠏"};
        int num_frames = 10;
        // Calculate frame based on time to animate
        int frame_idx =
            (current_time_us / 100000) % num_frames; // Change frame every 100ms

        printf("%s %-20.20s %6.2f%% %-12s                     ",
               spinner_frames[frame_idx], display_filename, percentage,
               speed_str); // Added spaces to clear
      } else {
        int num_blocks = (int)(percentage / 100.0 * bar_width);
        char colored_bracket[32]; // Buffer for colored bracket
        const char *color = ANSI_COLOR_GREEN;
        if (context->resume_offset > 0) {
          color = ANSI_COLOR_CYAN;
        }
        snprintf(colored_bracket, sizeof(colored_bracket), "[%s", color);
        printf("%-20.20s %s", display_filename, colored_bracket);

        for (int i = 0; i < num_blocks; i++)
          printf("\u2588");
        printf(ANSI_COLOR_RESET);
        for (int i = num_blocks; i < bar_width; i++)
          printf("\u2591");

        printf("] %6.2f%% %-12s", percentage, speed_str);
        if (context->resume_offset > 0) {
          printf(" (Resumed)");
        }
      }
    }
    break;
  }
  case STATUS_FAILED: {
    // Draw a "Failed" message
    char colored_bracket[32]; // Buffer for colored bracket
    snprintf(colored_bracket, sizeof(colored_bracket), "[%s", ANSI_COLOR_RED);

    if (g_no_progress_bar) {
      printf("%-20.20s [Failed (Code: %ld)]                           ",
             display_filename, context->response_code);
    } else {
      printf("%-20.20s %s", display_filename, colored_bracket);

      for (int i = 0; i < bar_width; i++)
        printf("!");
      printf(ANSI_COLOR_RESET "] Failed (Code: %ld)       ",
             context->response_code);
    }
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
  // Register signal handler for Ctrl+C
  signal(SIGINT, handle_sigint);

  // Variable to store the URL provided by the user
  char *url = NULL;
  // Array to store multiple URLs for multi-download mode
  char *initial_urls[1000]; // Max 1000 URLs
  int num_initial_urls = 0;
  int multi_mode = 0;                    // Flag for multi-download mode
  curl_off_t g_download_limit_bytes = 0; // Download limit per connection

  // Check if enough arguments are provided
  if (argc < 2) {
    fprintf(stderr, "Subliminal DownLoader version %s\n", SDL_VERSION);
    fprintf(stderr,
            "Usage: %s [options] [--url <URL> | --multi <URL1> ... | --file "
            "<file> | --add <URL> | --queue]\n",
            argv[0]);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -j, --jobs <mode>          Set download mode: 'code' "
                    "(sequential) or 'parallel' (simultaneous)\n");
    fprintf(stderr, "  -f, --file <file>          Read URLs from a file\n");
    fprintf(stderr, "  -d, --destination <dir>    Set destination directory\n");
    fprintf(stderr, "  -l, --limit-download <MB>  Limit download speed per "
                    "connection (e.g. 1.5)\n");
    fprintf(stderr,
            "  -aw, --always-overwrite    Always overwrite existing files\n");
    fprintf(stderr,
            "  -a, --add <URL>            Add URL to queue (dburl.txt)\n");
    fprintf(stderr, "  -Q, --queue                Process queue (dburl.txt) "
                    "and remove on success\n");
    fprintf(stderr,
            "  --nobar                    Disable progress bar rendering\n");
    fprintf(stderr, "  -v, --version              Show version\n");
    return EXIT_FAILURE; // Exit with an error code
  }

  // Loop through command-line arguments
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-v") == 0) {
      printf("Subliminal DownLoader version %s\n", SDL_VERSION);
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
    } else if (strcmp(argv[i], "--jobs") == 0 || strcmp(argv[i], "-j") == 0) {
      if (++i < argc) {
        if (strcmp(argv[i], "code") == 0 || strcmp(argv[i], "queue") == 0 ||
            strcmp(argv[i], "sequential") == 0) {
          g_max_parallel = 1;
        } else if (strcmp(argv[i], "parallel") == 0) {
          g_max_parallel = 1000;
        } else {
          fprintf(stderr,
                  "Error: Invalid argument for %s. Use 'code' or 'parallel'.\n",
                  argv[i - 1]);
          return EXIT_FAILURE;
        }
      } else {
        fprintf(stderr, "Error: No mode provided after %s\n", argv[i - 1]);
        return EXIT_FAILURE;
      }
    } else if (strcmp(argv[i], "--url") == 0 || strcmp(argv[i], "-u") == 0) {
      if (++i < argc) {
        url = argv[i];
        num_initial_urls = 1;
      } else {
        fprintf(stderr, "Error: No URL provided after %s\n", argv[i]);
        return EXIT_FAILURE;
      }
    } else if (strcmp(argv[i], "--multi") == 0 || strcmp(argv[i], "-m") == 0) {
      multi_mode = 1;
      // Collect all subsequent arguments as URLs
      while (++i < argc && argv[i][0] != '-') {
        if (num_initial_urls < 1000) {
          initial_urls[num_initial_urls++] = argv[i];
        }
      }
      i--; // Decrement because the loop will increment it

      if (num_initial_urls == 0) {
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

          if (num_initial_urls < 1000) {
            initial_urls[num_initial_urls++] = strdup(p);
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
    } else if (strcmp(argv[i], "--add") == 0 || strcmp(argv[i], "-a") == 0) {
      if (++i < argc) {
        add_url_to_db(argv[i]);
        return EXIT_SUCCESS;
      } else {
        fprintf(stderr, "Error: No URL provided after %s\n", argv[i - 1]);
        return EXIT_FAILURE;
      }
    } else if (strcmp(argv[i], "--queue") == 0 || strcmp(argv[i], "-Q") == 0) {
      g_queue_mode = 1;
      // Load URLs from DB_FILENAME
      FILE *file = fopen(DB_FILENAME, "r");
      if (!file) {
        fprintf(stderr, "Error: Could not open queue file '%s'\n", DB_FILENAME);
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

        if (num_initial_urls < 1000) {
          initial_urls[num_initial_urls++] = strdup(p);
        } else {
          fprintf(stderr, "Warning: Maximum number of URLs (1000) reached. "
                          "Ignoring remaining.\n");
          break;
        }
      }
      fclose(file);
      multi_mode = 1; // Treat as multi-mode
    } else if (strcmp(argv[i], "--nobar") == 0) {
      g_no_progress_bar = 1;
    } else if (strcmp(argv[i], "--limit-download") == 0 ||
               strcmp(argv[i], "-l") == 0) {
      if (++i < argc) {
        double limit_mb = atof(argv[i]);
        if (limit_mb <= 0) {
          fprintf(stderr, "Error: Invalid limit value. Must be > 0 MB/s\n");
          return EXIT_FAILURE;
        }
        g_download_limit_bytes = (curl_off_t)(limit_mb * 1024 * 1024);
      } else {
        fprintf(stderr, "Error: No limit provided after %s\n", argv[i - 1]);
        return EXIT_FAILURE;
      }
    }
  }

  // Check if any URL was provided
  if (num_initial_urls == 0 && url == NULL) {
    fprintf(stderr, "Error: No URL(s) provided. Use --url or --multi.\n");
    return EXIT_FAILURE;
  }

  // If not in multi_mode, but a single url was provided
  if (!multi_mode && url != NULL) {
    initial_urls[0] = url;
  }

  // --- URL Resolution Step ---
  char *urls[1000];
  int num_urls = 0;
  resolve_urls(initial_urls, num_initial_urls, urls, &num_urls, 1000);

  // Free the token now that resolution is done
  if (g_gofile_token) {
    free(g_gofile_token);
    g_gofile_token = NULL;
  }

  if (num_urls == 0) {
    printf("No downloadable files found after resolving URLs.\n");
    return EXIT_SUCCESS;
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

    // Generate .part filename
    size_t part_len = strlen(contexts[i].filename) + 6; // + ".part" + '\0'
    contexts[i].part_filename = malloc(part_len);
    snprintf(contexts[i].part_filename, part_len, "%s.part",
             contexts[i].filename);

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
        // Update part_filename
        free(contexts[i].part_filename);
        size_t part_len = strlen(contexts[i].filename) + 6;
        contexts[i].part_filename = malloc(part_len);
        snprintf(contexts[i].part_filename, part_len, "%s.part",
                 contexts[i].filename);
        printf("Will save as '%s'\n", contexts[i].filename);
        break;
      }
      case 's':
      case 'S':
        printf("Skipping download for '%s'\n", urls[i]);
        free(contexts[i].filename);
        contexts[i].filename = NULL;
        free(contexts[i].part_filename);
        contexts[i].part_filename = NULL;
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

    // Check for existing partial file to resume
    struct stat st;
    if (stat(contexts[i].part_filename, &st) == 0) {
      contexts[i].resume_offset = st.st_size;
      contexts[i].fp = fopen(contexts[i].part_filename, "ab");
    } else {
      contexts[i].resume_offset = 0;
      contexts[i].fp = fopen(contexts[i].part_filename, "wb");
    }

    if (!contexts[i].fp) {
      fprintf(stderr, "Error: Could not open file %s for writing.\n",
              contexts[i].part_filename);
      free(contexts[i].filename);
      contexts[i].filename = NULL;
      free(contexts[i].part_filename);
      contexts[i].part_filename = NULL;
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
    if (contexts[i].resume_offset > 0) {
      curl_easy_setopt(contexts[i].easy_handle, CURLOPT_RESUME_FROM_LARGE,
                       contexts[i].resume_offset);
    }
    curl_easy_setopt(contexts[i].easy_handle, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(contexts[i].easy_handle, CURLOPT_XFERINFOFUNCTION,
                     progress_callback);

    // Pass the context for this transfer to the progress callback
    curl_easy_setopt(contexts[i].easy_handle, CURLOPT_XFERINFODATA,
                     &contexts[i]);

    // Apply download speed limit if set
    if (g_download_limit_bytes > 0) {
      curl_easy_setopt(contexts[i].easy_handle, CURLOPT_MAX_RECV_SPEED_LARGE,
                       g_download_limit_bytes);
    }

    // Don't add to multi_handle here yet. We do it in the loop based on
    // g_max_parallel
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

  // Hide cursor for clean progress bar rendering
  printf("\x1b[?25l");

  // --- Perform the transfers ---
  int transfers_running = 0;
  int transfers_index = 0;

  // Initial fill of the queue
  while (transfers_index < num_urls && transfers_running < g_max_parallel) {
    if (contexts[transfers_index]
            .easy_handle) { // Only add if not skipped/failed during setup
      curl_multi_add_handle(multi_handle,
                            contexts[transfers_index].easy_handle);
      transfers_running++;
    }
    transfers_index++;
  }

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

    // Check for finished transfers to refill the queue
    CURLMsg *msg;
    int msgs_left;
    while ((msg = curl_multi_info_read(multi_handle, &msgs_left))) {
      if (msg->msg == CURLMSG_DONE) {
        CURL *easy_handle = msg->easy_handle;
        CURLcode result = msg->data.result;

        // Remove the finished handle
        curl_multi_remove_handle(multi_handle, easy_handle);
        transfers_running--;

        // Find which transfer this message belongs to
        for (int i = 0; i < num_urls; i++) {
          if (contexts[i].easy_handle == easy_handle) {
            long response_code = 0;
            curl_easy_getinfo(easy_handle, CURLINFO_RESPONSE_CODE,
                              &response_code);
            contexts[i].response_code = response_code;

            if (contexts[i].restart_needed) {
              // Handle restart
              curl_multi_remove_handle(multi_handle, easy_handle);
              transfers_running--;

              // Reset context
              contexts[i].restart_needed = 0;
              contexts[i].resume_offset = 0;
              contexts[i].status = STATUS_PENDING;

              // Truncate file
              if (contexts[i].fp) {
                fclose(contexts[i].fp);
                contexts[i].fp = fopen(contexts[i].part_filename, "wb");
              }

              // Update curl options
              curl_easy_setopt(contexts[i].easy_handle,
                               CURLOPT_RESUME_FROM_LARGE, (curl_off_t)0);
              curl_easy_setopt(contexts[i].easy_handle, CURLOPT_WRITEDATA,
                               contexts[i].fp);

              // Add back to queue (by resetting index or re-adding
              // immediately?) We can just re-add it immediately if we want to
              // keep slot
              curl_multi_add_handle(multi_handle, contexts[i].easy_handle);
              transfers_running++;
              curl_multi_perform(multi_handle, &still_running);
              break; // Continue outer loop
            }

            if (result == CURLE_OK &&
                ((response_code >= 200 && response_code < 300) ||
                 response_code == 206)) {
              contexts[i].status = STATUS_SUCCESS;
              if (contexts[i].fp) {
                fclose(contexts[i].fp);
                contexts[i].fp = NULL;
              }
              if (rename(contexts[i].part_filename, contexts[i].filename) !=
                  0) {
                fprintf(stderr, "Error renaming %s to %s\n",
                        contexts[i].part_filename, contexts[i].filename);
                contexts[i].status = STATUS_FAILED;
              } else {
                // Success!
                if (g_queue_mode) {
                  remove_url_from_db(urls[i]);
                }
              }
            } else {
              contexts[i].status = STATUS_FAILED;

              int should_remove_from_queue = 0;

              // Check for fatal errors to remove from queue
              if (result != CURLE_OK) {
                // If it's NOT a connection/temporary error, assume it's fatal
                // (e.g. malformed URL)
                if (result != CURLE_COULDNT_CONNECT &&
                    result != CURLE_COULDNT_RESOLVE_HOST &&
                    result != CURLE_OPERATION_TIMEDOUT &&
                    result != CURLE_GOT_NOTHING && result != CURLE_RECV_ERROR) {
                  should_remove_from_queue = 1;
                }
              } else {
                // It was an HTTP error code
                if (response_code >= 400 && response_code < 500) {
                  // Client error (404 Not Found, 410 Gone, 403 Forbidden, etc.)
                  // -> Remove
                  should_remove_from_queue = 1;
                }
                // 5xx errors are Server Errors, might be temporary, so we KEEP
                // them.
              }

              if (g_queue_mode && should_remove_from_queue) {
                printf("\nRemoving invalid/failed URL from queue: %s (Code: "
                       "%ld, Result: %d)\n",
                       urls[i], response_code, result);
                remove_url_from_db(urls[i]);
              }

              // If failed, we keep the part file to allow resume later, UNLESS
              // it's a client/server error that suggests the file is invalid or
              // gone (4xx, 5xx). Exception: 416 Range Not Satisfiable (maybe
              // file changed/finished?) For now, remove if >= 400.
              if (response_code >= 400) {
                remove(contexts[i].part_filename);
              }

              if (contexts[i].fp) {
                fclose(contexts[i].fp);
                contexts[i].fp = NULL;
              }
            }
            break;
          }
        }

        // Add next transfer if available
        while (transfers_index < num_urls &&
               transfers_running < g_max_parallel) {
          if (contexts[transfers_index]
                  .easy_handle) { // Only add if not skipped/failed during setup
            curl_multi_add_handle(multi_handle,
                                  contexts[transfers_index].easy_handle);
            transfers_running++;
            // We need to kick start the new handle
            curl_multi_perform(multi_handle, &still_running);
          }
          transfers_index++;
        }
      }
    }
  } while (still_running ||
           transfers_index <
               num_urls); // Continue if running OR if items left in queue

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
      if (g_no_progress_bar) {
        printf("%-20.20s [Completed]                                   ",
               display_filename);
      } else {
        printf("%-20.20s ", display_filename); // Print filename and space
        printf("[%s", ANSI_COLOR_GREEN);       // Print colored opening bracket
        for (int k = 0; k < bar_width; k++)
          printf("\u2588");
        printf(ANSI_COLOR_RESET
               "] [Completed]               "); // Clear the rest of the line
      }
      break;
    case STATUS_FAILED:
      if (g_no_progress_bar) {
        printf("%-20.20s [Failed (Code: %ld)]                           ",
               display_filename, contexts[i].response_code);
      } else {
        printf("%-20.20s ", display_filename); // Print filename and space
        printf("[%s", ANSI_COLOR_RED);         // Print colored opening bracket
        for (int k = 0; k < bar_width; k++)
          printf("!");
        printf(ANSI_COLOR_RESET "] [Failed (Code: %ld)]",
               contexts[i].response_code);
      }
      break;
    case STATUS_PENDING:     // Should not happen for active transfers, but for
                             // robustness
    case STATUS_DOWNLOADING: // Should now be finished, if not SUCCESS or FAILED
    default:
      if (g_no_progress_bar) {
        printf("%-20.20s [Skipped/Error]                               ",
               display_filename);
      } else {
        printf("%-20.20s [----------------------------------------] "
               "[Skipped/Error]       ",
               display_filename);
      }
      break;
    }
    // Move cursor back down
    printf("\x1b[%dB", g_total_downloads - contexts[i].line_number);
  }
  printf("\n"); // Final newline to push the prompt below the output

  // --- Cleanup ---
  show_cursor();
  for (int i = 0; i < num_urls; i++) {
    if (contexts[i].easy_handle) {
      // curl_multi_remove_handle might fail if it was already removed, but
      // that's fine/safe
      curl_multi_remove_handle(multi_handle, contexts[i].easy_handle);
      curl_easy_cleanup(contexts[i].easy_handle);
    }
    if (contexts[i].fp)
      fclose(contexts[i].fp);
    if (contexts[i].filename)
      free(contexts[i].filename); // Always free filename
    if (contexts[i].part_filename)
      free(contexts[i].part_filename);
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
