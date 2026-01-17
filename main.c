#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <errno.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>


#define GAME_DIR "release"
#define CACHE_DIR "cache"
#define JAVA_EXEC "java"  // Search java in path
#define PWR_URL "https://game-patches.hytale.com/patches/linux/amd64/release/0/1.pwr"
#define PWR_FILE CACHE_DIR "/1.pwr"
#define STAGING_DIR "staging_directory"
#define PLATFORM 0 // Linux -> 0, Windows -> 1, MacOS -> 2

// 3 patches URLs
const char *pwr_urls[] = {
    "https://game-patches.hytale.com/patches/linux/amd64/release/0/1.pwr",
    "https://game-patches.hytale.com/patches/linux/amd64/release/0/2.pwr",
    "https://game-patches.hytale.com/patches/linux/amd64/release/0/3.pwr"
};

// Local filenames
const char *pwr_files[] = {
    CACHE_DIR "/1.pwr",
    CACHE_DIR "/2.pwr",
    CACHE_DIR "/3.pwr"
};

int apply_pwr_with_butler(const char *butler_path, const char *pwr_file, const char *game_dir, const char *staging_dir) {
    pid_t pid = fork();
    if(pid == 0) {
        char *args[] = {
            (char *)butler_path,
            "apply",
            "--staging-dir",
            (char *)staging_dir,
            (char *)pwr_file,
            (char *)game_dir,
            NULL
        };
        execv(butler_path, args);
        perror("execv failed");
        exit(1);
    } else if(pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        if(WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            printf("Patch %s applied correctly\n", pwr_file);
            return 0;
        } else {
            fprintf(stderr, "Butler failed to apply %s\n", pwr_file);
            return -1;
        }
    } else {
        perror("fork failed");
        return -1;
    }
}

// Recursive directory creator
void create_dir(const char *path) {
    char tmp[512];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp),"%s", path);
    len = strlen(tmp);
    if(tmp[len - 1] == '/') tmp[len - 1] = 0;

    for(p = tmp + 1; *p; p++) {
        if(*p == '/') {
            *p = 0;
            mkdir(tmp, S_IRWXU);
            *p = '/';
        }
    }
    mkdir(tmp, S_IRWXU);
}

// Callback curl function to write the file
size_t write_data(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    return fwrite(ptr, size, nmemb, stream);
}

// Downloads a file from a URL
int download_file(const char *url, const char *dest) {
    CURL *curl = curl_easy_init();
    if(!curl) return -1;

    FILE *fp = fopen(dest, "wb");
    if(!fp) {
        perror("fopen");
        return -1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    fclose(fp);

    if(res != CURLE_OK) {
        fprintf(stderr, "curl error: %s\n", curl_easy_strerror(res));
        return -1;
    }

    return 0;
}

// Detects java PATH
int detect_java(char *java_path, size_t size) {
    FILE *fp = popen("which java", "r");
    if(!fp) return -1;
    if(fgets(java_path, size, fp) == NULL) {
        pclose(fp);
        return -1;
    }
    pclose(fp);
    java_path[strcspn(java_path, "\n")] = 0; // remove newline
    return 0;
}

// Launches the game

int launch_game(const char *java_path, const char *name) {
    char name_param[128];
    char java_param[512];

    snprintf(name_param, sizeof(name_param), "--name=%s", name);
    snprintf(java_param, sizeof(java_param), "--java-exec=%s", java_path);

    // The first argument MUST be the executable itself
    char *args[] = { 
        "HytaleClient", // argv[0]
        "--auth-mode=offline",
        "--uuid=34ef6c2d-13be-3de0-ba01-6bca1d9af96b",
        name_param,
        java_param,
        NULL
    };

    if (PLATFORM == 0) {
        execvp("release/Client/HytaleClient", args);
    } else if (PLATFORM == 1) {
        execvp("release/Client/HytaleClient.exe", args);
    }

    perror("execvp"); // only reached if execvp fails
    return -1;
}


// Checks weather a directory exists
bool directory_exists(const char *path){
    struct stat info;

    if (stat(path, &info) != 0)
        return false;

    return S_ISDIR(info.st_mode);
}

// Creates folders, downloads the game if not done already, etc.
void prepare_enviroment(){
    // Create folders
    if (!directory_exists(GAME_DIR)){
        create_dir(GAME_DIR);
    }
    if (!directory_exists(CACHE_DIR)){
        create_dir(CACHE_DIR);
    }
    if (!directory_exists(STAGING_DIR)){
        create_dir(STAGING_DIR);
    }

    // Download patches if they don't exist
    struct stat st = {0};
    for(int i = 0; i < 3; i++) {
        struct stat st = {0};
        if(stat(pwr_files[i], &st) == -1) {
            printf("Downloading patch %d...\n", i+1);
            if(download_file(pwr_urls[i], pwr_files[i]) != 0) {
                fprintf(stderr, "Error when downloading %s\n", pwr_files[i]);
            }
            printf("Patch %d downloaded in %s\n", i+1, pwr_files[i]);
        } else {
            printf("Patch %d already exists: %s\n", i+1, pwr_files[i]);
        }
    }
    // Apply patches
    if (!directory_exists("release/Client")){
        for(int i = 0; i < 3; i++) {
            if (PLATFORM == 0){
                if(apply_pwr_with_butler("butler/butler_linux/butler", pwr_files[i], "release", "staging_directory") != 0) {
                    fprintf(stderr, "Failed to apply patch %d\n", i+1);
                }
            }else if (PLATFORM == 1){
                if(apply_pwr_with_butler("butler/butler_windows/butler.exe", pwr_files[i], "release", "staging_directory") != 0) {
                    fprintf(stderr, "Failed to apply patch %d\n", i+1);
                }
            }
        }
    }
}

int copy_file(const char *src, const char *dst, mode_t mode){
    int in, out;
    ssize_t bytes;
    char buffer[8192];

    in = open(src, O_RDONLY);
    if (in < 0)
        return -1;

    out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (out < 0) {
        close(in);
        return -1;
    }

    while ((bytes = read(in, buffer, sizeof(buffer))) > 0) {
        if (write(out, buffer, bytes) != bytes) {
            close(in);
            close(out);
            return -1;
        }
    }

    close(in);
    close(out);
    return (bytes < 0) ? -1 : 0;
}

int copy_directory(const char *src, const char *dst){
    DIR *dir;
    struct dirent *entry;
    struct stat st;
    char src_path[4096];
    char dst_path[4096];

    if (stat(src, &st) < 0)
        return -1;

    /* Create destination directory */
    if (mkdir(dst, st.st_mode) < 0 && errno != EEXIST)
        return -1;

    dir = opendir(src);
    if (!dir)
        return -1;

    while ((entry = readdir(dir)) != NULL) {
        /* Skip . and .. */
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;

        snprintf(src_path, sizeof(src_path), "%s/%s", src, entry->d_name);
        snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, entry->d_name);

        if (stat(src_path, &st) < 0) {
            closedir(dir);
            return -1;
        }

        if (S_ISDIR(st.st_mode)) {
            /* Recursive copy for subdirectory */
            if (copy_directory(src_path, dst_path) < 0) {
                closedir(dir);
                return -1;
            }
        } else if (S_ISREG(st.st_mode)) {
            /* Copy regular file */
            if (copy_file(src_path, dst_path, st.st_mode) < 0) {
                closedir(dir);
                return -1;
            }
        }
        /* Symlinks, devices, etc. are ignored */
    }

    closedir(dir);
    return 0;
}

int delete_directory(const char *path) {
    struct dirent *entry;
    struct stat statbuf;
    DIR *dir = opendir(path);

    if (!dir) {
        perror("opendir failed");
        return -1;
    }

    while ((entry = readdir(dir)) != NULL) {
        char fullpath[1024];

        // Skip "." and ".." entries
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        snprintf(fullpath, sizeof(fullpath), "%s/%s", path, entry->d_name);

        if (stat(fullpath, &statbuf) == -1) {
            perror("stat failed");
            closedir(dir);
            return -1;
        }

        if (S_ISDIR(statbuf.st_mode)) {
            // Recursively delete subdirectory
            if (delete_directory(fullpath) != 0) {
                closedir(dir);
                return -1;
            }
        } else {
            // Delete file
            if (remove(fullpath) != 0) {
                perror("remove failed");
                closedir(dir);
                return -1;
            }
        }
    }

    closedir(dir);

    // Delete the now-empty directory
    if (rmdir(path) != 0) {
        perror("rmdir failed");
        return -1;
    }

    return 0;
}

int main() {
    char java_path[256] = "";
    char name[64] = "";
    int response = -1;

    do {
        printf("\nWelcome to Hylerty Launcher!\n");
        printf("Select one option:\n");
        printf("    1) Start Game\n");
        printf("    2) Select Username\n");
        printf("    3) Select Java Path\n");
        printf("    4) Install / Reinstall Game\n");
        printf("    0) Exit\n");

        scanf("%d", &response);

        switch (response) {

            case 0:
                printf("Exiting launcher...\n");
                break;

            case 1:
                printf("Preparing Environment...\n");
                prepare_enviroment();

                if (java_path[0] == '\0') {
                    if (PLATFORM == 0){
                        strcpy(java_path, "../../java/java25-linux/bin/java");
                    }else if(PLATFORM == 1){
                        strcpy(java_path, "../../java/java25-windows/bin/java.exe");
                    }
                }

                launch_game(java_path, name);
                break;

            case 2:
                printf("Introduce your username:\n");
                scanf("%15s", name);
                break;

            case 3:
                printf("Introduce your java path (or type PATH):\n");
                scanf("%255s", java_path);

                if (strcmp(java_path, "PATH") == 0) {
                    if (detect_java(java_path, sizeof(java_path)) != 0) {
                        fprintf(stderr, "Java not found in PATH\n");
                    } else {
                        printf("Java detected at: %s\n", java_path);
                    }
                }
                break;

            case 4:
                printf("Saving save files...\n");
                copy_directory("release/Client/UserData/Saves", "saves");
                printf("Deleting directories");
                delete_directory(GAME_DIR);
                delete_directory(CACHE_DIR);
                delete_directory(STAGING_DIR);
                prepare_enviroment();
                copy_directory("saves", "release/Client/UserData/Saves");
                break;

            default:
                printf("Invalid option. Try again.\n");
        }

    } while (response != 0);

    return 0;
}
