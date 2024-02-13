#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <locale.h>
#include <time.h>

#include "tabellinator.c"

char out_file_path[128] = {0};

int file_exists(const char *path) {
    FILE *file;
    if ((file = fopen(path, "r")))
    {
        fclose(file);
        return 1;
    }
    return 0;
}

int load_source(const char* path) {
    FILE *fp = fopen(path, "r");

    if (fp != NULL) {
        source_size = fread(source, sizeof(char), MAX_SOURCE_LEN, fp);
        if (ferror( fp ) != 0) {
            fprintf(stderr, "[ERROR] Could not load source from `%s`.\n", path);
            fclose(fp);
            return -1;
        } else {
            source[source_size++] = '\0';
        }

        fclose(fp);
    } else {
        fprintf(stderr, "[ERROR] Could not open file `%s`.\n", path);
    }

    return 0;
}

void compile_latex() {
    char command[256] = {0};
    snprintf(command, 256, "xelatex -interaction=nonstopmode '%s'"
#ifdef _WIN32
    " > nul"
#else
    " > /dev/null"
#endif
    , out_file_path);
    system(command);
}

void print_usage(const char* program) {
    printf("USAGE: %s <path/to/file.gpx>\n", program);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "[ERROR] Pathath to GPX file not provided.\n");
        print_usage(argv[0]);
        return 0;
    }

    char* file_path = argv[1];

    if (strcmp(file_path+strlen(file_path)-4, ".gpx") != 0) {
        fprintf(stderr, "[ERROR] Provided file was not GPX.\n");
        return 1;
    }
    snprintf(out_file_path, 128, "%.*s.tex", (int) strlen(file_path)-4, file_path);

    {
        double factor = 0;
        double pause_factor = 0;
        uint64_t hours = 0, mins = 0;
        printf("Vi prego d'inserire:\n");

        printf(" - Fattore di marcia (kms/h): ");
        scanf("%lf", &factor);
        if (factor > 0) FACTOR = factor;

        printf(" - Fattore di pausa (min/h): ");
        scanf("%lf", &pause_factor);
        if (pause_factor > 0) PAUSE_FACTOR = pause_factor/60.0;

        printf(" - Orario di partenza [hh:mm]: ");
        scanf("%zu:%zu", &hours, &mins);
        if ( (int64_t) hours >= 0 && (int64_t) mins >= 0) START_TIME = hours * 60 + mins;
        // printf("%f %f %ld:%ld\n\n\n", factor, pause_factor, hours, mins);
    }

    if (load_source(file_path) != 0) {
        return 1;
    }

    uint8_t* error_free_source = fix_source((uint8_t*) source+1);
    printf("%ld\n", error_free_source-(uint8_t*)source);

    parse_gpx(error_free_source, file_path);

    // Output table
    // Output graph
    // Output latex doc
    FILE *out_file = fopen(out_file_path, "w");
    if (out_file == NULL) {
        out_file = stdout;
    }
    
    print_latex_document(out_file);

    // Create PDF
    if (out_file != stdout) {
        fclose(out_file);
        compile_latex();
    }

    return 0;
}