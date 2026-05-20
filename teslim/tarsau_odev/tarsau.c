#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_FILES 32
#define MAX_TOTAL_SIZE (200LL * 1024LL * 1024LL)
#define HEADER_WIDTH 10
#define COPY_BUFFER_SIZE 65536

typedef struct {
    char path[PATH_MAX];
    char name[NAME_MAX + 1];
    mode_t mode;
    long long size;
} InputFile;

typedef struct {
    char name[NAME_MAX + 1];
    mode_t mode;
    long long size;
} ArchiveEntry;

static void print_usage(void) {
    fprintf(stderr, "Kullanim:\n");
    fprintf(stderr, "  tarsau -b dosya1 dosya2 ... [-o arsiv.sau]\n");
    fprintf(stderr, "  tarsau -a arsiv.sau [dizin]\n");
}

static int has_sau_extension(const char *path) {
    size_t len = strlen(path);
    return len > 4 && strcmp(path + len - 4, ".sau") == 0;
}

static const char *base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static int is_safe_archive_name(const char *name) {
    if (name[0] == '\0' || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return 0;
    }
    if (strchr(name, '/') || strchr(name, '\\') || strchr(name, ',') || strchr(name, '|')) {
        return 0;
    }
    if (strstr(name, "..")) {
        return 0;
    }
    return 1;
}

static int copy_stream(FILE *in, FILE *out, long long bytes_to_copy) {
    unsigned char buffer[COPY_BUFFER_SIZE];

    while (bytes_to_copy > 0) {
        size_t want = bytes_to_copy > COPY_BUFFER_SIZE ? COPY_BUFFER_SIZE : (size_t) bytes_to_copy;
        size_t got = fread(buffer, 1, want, in);
        if (got == 0) {
            return -1;
        }
        if (fwrite(buffer, 1, got, out) != got) {
            return -1;
        }
        bytes_to_copy -= (long long) got;
    }

    return 0;
}

static int validate_ascii_text_file(const char *path, long long *size_out) {
    FILE *file = fopen(path, "rb");
    unsigned char buffer[COPY_BUFFER_SIZE];
    long long size = 0;

    if (!file) {
        return -1;
    }

    for (;;) {
        size_t got = fread(buffer, 1, sizeof(buffer), file);
        if (got > 0) {
            for (size_t i = 0; i < got; ++i) {
                if (buffer[i] == 0 || buffer[i] > 127) {
                    fclose(file);
                    return 1;
                }
            }
            size += (long long) got;
        }

        if (got < sizeof(buffer)) {
            if (ferror(file)) {
                fclose(file);
                return -1;
            }
            break;
        }
    }

    fclose(file);
    *size_out = size;
    return 0;
}

static int append_metadata(char **metadata, size_t *length, size_t *capacity, const InputFile *file) {
    char record[PATH_MAX + 64];
    int written = snprintf(record, sizeof(record), "|%s,%04o,%lld|",
                           file->name, (unsigned int) (file->mode & 0777), file->size);

    if (written < 0 || (size_t) written >= sizeof(record)) {
        return -1;
    }

    if (*length + (size_t) written + 1 > *capacity) {
        size_t new_capacity = *capacity == 0 ? 256 : *capacity;
        while (*length + (size_t) written + 1 > new_capacity) {
            new_capacity *= 2;
        }
        char *new_metadata = realloc(*metadata, new_capacity);
        if (!new_metadata) {
            return -1;
        }
        *metadata = new_metadata;
        *capacity = new_capacity;
    }

    memcpy(*metadata + *length, record, (size_t) written);
    *length += (size_t) written;
    (*metadata)[*length] = '\0';
    return 0;
}

static int build_archive(int argc, char **argv) {
    InputFile files[MAX_FILES];
    int file_count = 0;
    const char *output_path = "a.sau";
    long long total_size = 0;
    char *metadata = NULL;
    size_t metadata_length = 0;
    size_t metadata_capacity = 0;
    FILE *out = NULL;
    char header[HEADER_WIDTH + 1];

    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                print_usage();
                return 1;
            }
            output_path = argv[++i];
            continue;
        }

        if (file_count >= MAX_FILES) {
            fprintf(stderr, "Giris dosyasi sayisi en fazla 32 olabilir!\n");
            return 1;
        }

        strncpy(files[file_count].path, argv[i], sizeof(files[file_count].path) - 1);
        files[file_count].path[sizeof(files[file_count].path) - 1] = '\0';
        ++file_count;
    }

    if (file_count == 0) {
        print_usage();
        return 1;
    }

    for (int i = 0; i < file_count; ++i) {
        struct stat st;
        const char *name = base_name(files[i].path);
        long long size = 0;
        int validation_result;

        if (!is_safe_archive_name(name)) {
            fprintf(stderr, "%s dosya adi arsiv formati icin uygunsuzdur!\n", files[i].path);
            return 1;
        }

        if (stat(files[i].path, &st) != 0 || !S_ISREG(st.st_mode)) {
            fprintf(stderr, "%s giris dosyasi okunamadi!\n", files[i].path);
            return 1;
        }

        validation_result = validate_ascii_text_file(files[i].path, &size);
        if (validation_result == 1) {
            printf("%s giriş dosyasının formatı uyumsuzdur!\n", files[i].path);
            return 1;
        }
        if (validation_result != 0) {
            fprintf(stderr, "%s giris dosyasi okunamadi!\n", files[i].path);
            return 1;
        }

        total_size += size;
        if (total_size > MAX_TOTAL_SIZE) {
            fprintf(stderr, "Giris dosyalarinin toplam boyutu 200 MB'i gecemez!\n");
            return 1;
        }

        strncpy(files[i].name, name, sizeof(files[i].name) - 1);
        files[i].name[sizeof(files[i].name) - 1] = '\0';
        for (int j = 0; j < i; ++j) {
            if (strcmp(files[j].name, files[i].name) == 0) {
                fprintf(stderr, "%s dosya adi arsiv icinde tekrar ediyor!\n", files[i].name);
                free(metadata);
                return 1;
            }
        }
        files[i].mode = st.st_mode & 0777;
        files[i].size = size;

        if (append_metadata(&metadata, &metadata_length, &metadata_capacity, &files[i]) != 0) {
            fprintf(stderr, "Bellek ayrilamadi!\n");
            free(metadata);
            return 1;
        }
    }

    if (HEADER_WIDTH + metadata_length > 9999999999ULL) {
        fprintf(stderr, "Organizasyon bolumu cok buyuk!\n");
        free(metadata);
        return 1;
    }

    snprintf(header, sizeof(header), "%010zu", HEADER_WIDTH + metadata_length);
    out = fopen(output_path, "wb");
    if (!out) {
        fprintf(stderr, "%s arsiv dosyasi olusturulamadi!\n", output_path);
        free(metadata);
        return 1;
    }

    if (fwrite(header, 1, HEADER_WIDTH, out) != HEADER_WIDTH ||
        fwrite(metadata, 1, metadata_length, out) != metadata_length) {
        fprintf(stderr, "%s arsiv dosyasina yazilamadi!\n", output_path);
        fclose(out);
        free(metadata);
        return 1;
    }

    for (int i = 0; i < file_count; ++i) {
        FILE *in = fopen(files[i].path, "rb");
        if (!in) {
            fprintf(stderr, "%s giris dosyasi okunamadi!\n", files[i].path);
            fclose(out);
            free(metadata);
            return 1;
        }
        if (copy_stream(in, out, files[i].size) != 0) {
            fprintf(stderr, "Arsivleme sirasinda hata olustu!\n");
            fclose(in);
            fclose(out);
            free(metadata);
            return 1;
        }
        fclose(in);
    }

    fclose(out);
    free(metadata);
    printf("Dosyalar birleştirildi.\n");
    return 0;
}

static int mkdir_p(const char *path) {
    char temp[PATH_MAX];
    size_t len;

    if (path[0] == '\0' || strcmp(path, ".") == 0) {
        return 0;
    }

    strncpy(temp, path, sizeof(temp) - 1);
    temp[sizeof(temp) - 1] = '\0';
    len = strlen(temp);
    if (len == 0) {
        return 0;
    }
    if (temp[len - 1] == '/') {
        temp[len - 1] = '\0';
    }

    for (char *p = temp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(temp, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }

    if (mkdir(temp, 0755) != 0 && errno != EEXIST) {
        return -1;
    }

    struct stat st;
    return stat(temp, &st) == 0 && S_ISDIR(st.st_mode) ? 0 : -1;
}

static int parse_long_long(const char *text, long long *value_out) {
    char *end = NULL;
    errno = 0;
    long long value = strtoll(text, &end, 10);
    if (errno != 0 || !end || *end != '\0' || value < 0) {
        return -1;
    }
    *value_out = value;
    return 0;
}

static int parse_mode_octal(const char *text, mode_t *mode_out) {
    char *end = NULL;
    errno = 0;
    long value = strtol(text, &end, 8);
    if (errno != 0 || !end || *end != '\0' || value < 0 || value > 0777) {
        return -1;
    }
    *mode_out = (mode_t) value;
    return 0;
}

static int parse_metadata(char *metadata, size_t metadata_length, ArchiveEntry entries[], int *count_out) {
    size_t pos = 0;
    int count = 0;

    while (pos < metadata_length) {
        char *name_start;
        char *comma1;
        char *comma2;
        char *end_bar;
        ArchiveEntry entry;
        long long size;
        mode_t mode;

        if (metadata[pos] != '|') {
            return -1;
        }
        name_start = metadata + pos + 1;
        comma1 = strchr(name_start, ',');
        if (!comma1) {
            return -1;
        }
        comma2 = strchr(comma1 + 1, ',');
        if (!comma2) {
            return -1;
        }
        end_bar = strchr(comma2 + 1, '|');
        if (!end_bar) {
            return -1;
        }

        *comma1 = '\0';
        *comma2 = '\0';
        *end_bar = '\0';

        if (count >= MAX_FILES || !is_safe_archive_name(name_start) ||
            parse_mode_octal(comma1 + 1, &mode) != 0 ||
            parse_long_long(comma2 + 1, &size) != 0) {
            return -1;
        }

        strncpy(entry.name, name_start, sizeof(entry.name) - 1);
        entry.name[sizeof(entry.name) - 1] = '\0';
        entry.mode = mode;
        entry.size = size;
        entries[count++] = entry;

        pos = (size_t) (end_bar - metadata) + 1;
    }

    *count_out = count;
    return count > 0 ? 0 : -1;
}

static int get_file_size(FILE *file, long long *size_out) {
    long current = ftell(file);
    long end;

    if (current < 0 || fseek(file, 0, SEEK_END) != 0) {
        return -1;
    }
    end = ftell(file);
    if (end < 0 || fseek(file, current, SEEK_SET) != 0) {
        return -1;
    }
    *size_out = (long long) end;
    return 0;
}

static int extract_archive(int argc, char **argv) {
    const char *archive_path;
    const char *target_dir = ".";
    FILE *archive = NULL;
    char header[HEADER_WIDTH + 1];
    char *metadata = NULL;
    long long section_size;
    long long archive_size;
    long long payload_size = 0;
    ArchiveEntry entries[MAX_FILES];
    int entry_count = 0;

    if (argc < 3 || argc > 4) {
        printf("Arşiv dosyası uygunsuz veya bozuk!\n");
        return 1;
    }

    archive_path = argv[2];
    if (argc == 4) {
        target_dir = argv[3];
    }

    if (!has_sau_extension(archive_path)) {
        printf("Arşiv dosyası uygunsuz veya bozuk!\n");
        return 1;
    }

    archive = fopen(archive_path, "rb");
    if (!archive) {
        printf("Arşiv dosyası uygunsuz veya bozuk!\n");
        return 1;
    }

    if (fread(header, 1, HEADER_WIDTH, archive) != HEADER_WIDTH) {
        printf("Arşiv dosyası uygunsuz veya bozuk!\n");
        fclose(archive);
        return 1;
    }
    header[HEADER_WIDTH] = '\0';
    for (int i = 0; i < HEADER_WIDTH; ++i) {
        if (header[i] < '0' || header[i] > '9') {
            printf("Arşiv dosyası uygunsuz veya bozuk!\n");
            fclose(archive);
            return 1;
        }
    }

    if (parse_long_long(header, &section_size) != 0 || section_size < HEADER_WIDTH) {
        printf("Arşiv dosyası uygunsuz veya bozuk!\n");
        fclose(archive);
        return 1;
    }

    if (get_file_size(archive, &archive_size) != 0 || archive_size < section_size) {
        printf("Arşiv dosyası uygunsuz veya bozuk!\n");
        fclose(archive);
        return 1;
    }

    metadata = malloc((size_t) (section_size - HEADER_WIDTH) + 1);
    if (!metadata) {
        fprintf(stderr, "Bellek ayrilamadi!\n");
        fclose(archive);
        return 1;
    }

    if (fread(metadata, 1, (size_t) (section_size - HEADER_WIDTH), archive) !=
        (size_t) (section_size - HEADER_WIDTH)) {
        printf("Arşiv dosyası uygunsuz veya bozuk!\n");
        free(metadata);
        fclose(archive);
        return 1;
    }
    metadata[section_size - HEADER_WIDTH] = '\0';

    if (parse_metadata(metadata, (size_t) (section_size - HEADER_WIDTH), entries, &entry_count) != 0) {
        printf("Arşiv dosyası uygunsuz veya bozuk!\n");
        free(metadata);
        fclose(archive);
        return 1;
    }

    for (int i = 0; i < entry_count; ++i) {
        payload_size += entries[i].size;
        if (payload_size > MAX_TOTAL_SIZE) {
            printf("Arşiv dosyası uygunsuz veya bozuk!\n");
            free(metadata);
            fclose(archive);
            return 1;
        }
    }

    if (section_size + payload_size != archive_size) {
        printf("Arşiv dosyası uygunsuz veya bozuk!\n");
        free(metadata);
        fclose(archive);
        return 1;
    }

    if (mkdir_p(target_dir) != 0) {
        fprintf(stderr, "%s dizini olusturulamadi!\n", target_dir);
        free(metadata);
        fclose(archive);
        return 1;
    }

    for (int i = 0; i < entry_count; ++i) {
        char output_path[PATH_MAX];
        FILE *out;

        if (strcmp(target_dir, ".") == 0) {
            snprintf(output_path, sizeof(output_path), "%s", entries[i].name);
        } else {
            snprintf(output_path, sizeof(output_path), "%s/%s", target_dir, entries[i].name);
        }

        out = fopen(output_path, "wb");
        if (!out) {
            fprintf(stderr, "%s dosyasi olusturulamadi!\n", output_path);
            free(metadata);
            fclose(archive);
            return 1;
        }

        if (copy_stream(archive, out, entries[i].size) != 0) {
            fprintf(stderr, "Arsiv acilirken hata olustu!\n");
            fclose(out);
            free(metadata);
            fclose(archive);
            return 1;
        }
        fclose(out);
        chmod(output_path, entries[i].mode);
    }

    printf("%s dizininde ", target_dir);
    for (int i = 0; i < entry_count; ++i) {
        printf("%s", entries[i].name);
        if (i + 2 < entry_count) {
            printf(", ");
        } else if (i + 1 < entry_count) {
            printf(" ve ");
        }
    }
    printf(" dosyaları açıldı.\n");

    free(metadata);
    fclose(archive);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    if (strcmp(argv[1], "-b") == 0) {
        return build_archive(argc, argv);
    }

    if (strcmp(argv[1], "-a") == 0) {
        return extract_archive(argc, argv);
    }

    print_usage();
    return 1;
}
