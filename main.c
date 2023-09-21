#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "xml.c"

#define FILE_PATH "path.gpx"
#define FACTOR 4 //kms/h
#define PAUSE_FACTOR (15.0 /* min/h */ /60.0) // -> min/min

typedef struct {
    double lat;
    double lon;
    double ele; // m
} Point;

typedef struct {
    double dst; // km
    double dh; // m
    double kms; // kms
    double t; // minutes
    double pause; // minutes
} PathSegmentData;

#define MAX_SOURCE_LEN 64 * 1000 * 1000
char source[MAX_SOURCE_LEN+1] = {0};
size_t source_size = 0;

#define MAX_STR_SIZE 128
char name[MAX_STR_SIZE+1] = {0};

#define WAYPOINTS_CAPACITY 128
Point waypoints[WAYPOINTS_CAPACITY] = {0};
size_t waypoints_len = 0;

#define PATH_CAPACITY 128 * 1000
Point path[PATH_CAPACITY] = {0};
size_t path_len = 0;

#define PATH_SEGMENTS_CAP WAYPOINTS_CAPACITY
PathSegmentData segments[PATH_SEGMENTS_CAP] = {0};
size_t segments_len = 0;

inline static double deg2rad(double a) {
    return (a / 180.0) * M_PI;
}

// source: https://en.wikipedia.org/wiki/Geographical_distance#Ellipsoidal_Earth_projected_to_a_plane
// note: best to use with distances < 475 km
// input in degrees
// output in km
double distance(const Point* a, const Point* b) {
    const double phia = a->lat;
    const double phib = b->lat;
    const double la = a->lon;
    const double lb = b->lon;

    const double deltaPhi = fabs(phia - phib);
    const double deltaLambda = fabs(la - lb);
    const double phi_m = (phia + phib) / 2.0;

    const double K1 = 111.13209 - 0.56605 * cos(2.0 * phi_m) + 0.00120 * cos(4.0 * phi_m);
    const double K2 = 111.41513 * cos(phi_m) - 0.09455 * cos(3.0 * phi_m) + 0.00012 * cos(5.0 * phi_m);

    const double A = (K1 * deltaPhi);
    const double B = (K2 * deltaLambda);
    return sqrt(A * A + B * B);
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

void extract_tour_name(struct xml_node* root) {
    struct xml_node* metadata_node = xml_node_child(root, 0);
    struct xml_node* name_node = xml_node_child(metadata_node, 2);; //xml_easy_child(root, "name");
    struct xml_string* name_str = xml_node_content(name_node);

    assert(xml_string_length(name_str) + 1 <= MAX_STR_SIZE+1 && "Tour name too long.");
	xml_string_copy(name_str, name, xml_string_length(name_str));
}

void extract_waypoints(struct xml_node* root) {
    size_t children = xml_node_children(root);
    for (size_t i = 1; i < children-1; i++) { // first is metadata, last is track
        Point* wp = &waypoints[waypoints_len++];
        assert(waypoints_len <= WAYPOINTS_CAPACITY);
        
        struct xml_node* waypoint_node = xml_node_child(root, i);
        


        for (size_t a = 0; a < xml_node_attributes(waypoint_node); a++) {
            struct xml_string* attr_name = xml_node_attribute_name(waypoint_node,a);
            uint8_t* attr_name_str = calloc(xml_string_length(attr_name) + 1, sizeof(uint8_t));
            xml_string_copy(attr_name, attr_name_str, xml_string_length(attr_name));

            if (strcmp(attr_name_str, "lon")==0) {
                struct xml_string* attr_content = xml_node_attribute_content(waypoint_node,a);
                uint8_t* attr_content_str = calloc(xml_string_length(attr_content) + 1, sizeof(uint8_t));
                xml_string_copy(attr_content, attr_content_str, xml_string_length(attr_content));

                wp->lon = atof(attr_content_str);

                free(attr_content_str);
            } else if (strcmp(attr_name_str, "lat")==0) {
                struct xml_string* attr_content = xml_node_attribute_content(waypoint_node,a);
                uint8_t* attr_content_str = calloc(xml_string_length(attr_content) + 1, sizeof(uint8_t));
                xml_string_copy(attr_content, attr_content_str, xml_string_length(attr_content));

                wp->lat = atof(attr_content_str);

                free(attr_content_str);
            }

            free(attr_name_str);
        }
        
        size_t cdren = xml_node_children(waypoint_node);
        for (size_t c = 0; c < cdren; ++c) {
            struct xml_node* child = xml_node_child(waypoint_node, c);

            struct xml_string* name = xml_node_name(child);
            uint8_t* name_str = calloc(xml_string_length(name) + 1, sizeof(uint8_t));
            xml_string_copy(name, name_str, xml_string_length(name));

            if (strcmp(name_str, "ele") == 0) {
                struct xml_string* content = xml_node_content(child);
                uint8_t* content_str = calloc(xml_string_length(content) + 1, sizeof(uint8_t));
                xml_string_copy(content, content_str, xml_string_length(content));

                wp->ele = atof(content_str);

                free(content_str);
                free(name_str);
                break;
            }

            free(name_str);
        }

        // printf("    wp-%d: %f %f %f\n", i, wp->lat, wp->lon, wp->ele);
    }
}

void extract_path(struct xml_node* track) {
    size_t children = xml_node_children(track);
    for (size_t i = 0; i < children; i++) { // first is metadata, last is track
        Point* p = &path[path_len++];
        // printf("%ld\n", path_len);
        assert(path_len <= PATH_CAPACITY);
        
        struct xml_node* point_node = xml_node_child(track, i);
        

        for (size_t a = 0; a < xml_node_attributes(point_node); a++) {
            struct xml_string* attr_name = xml_node_attribute_name(point_node,a);
            uint8_t* attr_name_str = calloc(xml_string_length(attr_name) + 1, sizeof(uint8_t));
            xml_string_copy(attr_name, attr_name_str, xml_string_length(attr_name));

            if (strcmp(attr_name_str, "lon")==0) {
                struct xml_string* attr_content = xml_node_attribute_content(point_node,a);
                uint8_t* attr_content_str = calloc(xml_string_length(attr_content) + 1, sizeof(uint8_t));
                xml_string_copy(attr_content, attr_content_str, xml_string_length(attr_content));

                p->lon = atof(attr_content_str);

                free(attr_content_str);
            } else if (strcmp(attr_name_str, "lat")==0) {
                struct xml_string* attr_content = xml_node_attribute_content(point_node,a);
                uint8_t* attr_content_str = calloc(xml_string_length(attr_content) + 1, sizeof(uint8_t));
                xml_string_copy(attr_content, attr_content_str, xml_string_length(attr_content));

                p->lat = atof(attr_content_str);

                free(attr_content_str);
            }

            free(attr_name_str);
        }
        
        size_t cdren = xml_node_children(point_node);
        for (size_t c = 0; c < cdren; ++c) {
            struct xml_node* child = xml_node_child(point_node, c);

            struct xml_string* name = xml_node_name(child);
            uint8_t* name_str = calloc(xml_string_length(name) + 1, sizeof(uint8_t));
            xml_string_copy(name, name_str, xml_string_length(name));

            if (strcmp(name_str, "ele") == 0) {
                struct xml_string* content = xml_node_content(child);
                uint8_t* content_str = calloc(xml_string_length(content) + 1, sizeof(uint8_t));
                xml_string_copy(content, content_str, xml_string_length(content));

                p->ele = atof(content_str);

                free(content_str);
                free(name_str);
                break;
            }

            free(name_str);
        }

        // printf("    p-%d: %f %f %f\n", i, p->lat, p->lon, p->ele);
    }
}

void calculate_path_segments_data() {
    assert(waypoints_len >= 2);
    size_t wp_idx = 1;
    segments_len = 1;
    for (size_t i = 1; i < path_len; i++) {
        assert(wp_idx < waypoints_len);
        PathSegmentData* ps = &segments[wp_idx-1];
        Point* pa = &path[i-1];
        Point* pb = &path[i];
        double dst = distance(pa, pb);
        double dh = pb->ele - pa->ele;
        double pendenza = dh/dst;
        double kms = dst;
        if (pendenza > 0) {
            kms += dh / 100.0;
        } else if (pendenza < -0.2) {
            kms += -dh / 150.0;
        }


        ps->dst += dst;
        ps->dh += dh;
        ps->kms += kms;

        if (distance(&waypoints[wp_idx], &path[i]) <= 0.0001) { // TODO: calculate min ddistance
            ps->t =  60.0 * (ps->kms / FACTOR);
            ps->pause = ps->t / PAUSE_FACTOR;
            wp_idx++;
            segments_len++;

            // {
            //     size_t min = (size_t) fmod(ps->t, 60.0);
            //     size_t hours = (size_t) (ps->t / 60.0);
            //     printf("%f km; %f m; %f kms; %f min (%02ldh %02ldm)\n", ps->dst, ps->dh, ps->kms, ps->t, hours, min);
            // }
        }
    }
}

int main(void) {
    if (load_source(FILE_PATH) != 0) {
        return 1;
    }

    char* error_free_source = source + 22; // for some reason '<?xml version="1.0"?>' makes the parser fail
    assert((*error_free_source == '<') && "Faulty source correction.");
    struct xml_document* document = xml_parse_document(error_free_source, strlen(error_free_source));
    if (!document) {
		fprintf(stderr, "[ERROR] Could parse file `%s`.\n", FILE_PATH);
		exit(EXIT_FAILURE);
	}

    struct xml_node* root = xml_document_root(document);

    size_t children = xml_node_children(root);

    // Retrieve tour name
    extract_tour_name(root);
    printf("Tour name:         '%s'\n", name);

    // Retrieve Waypoints
    extract_waypoints(root);
    printf("Waypoint count:     %ld\n", waypoints_len);

    // Retrieve path
    struct xml_node* track_container = xml_node_child(root, children-1);
    size_t track_container_children = xml_node_children(track_container);
    for (size_t c = 0; c < track_container_children; ++c) {
        struct xml_node* track = xml_node_child(track_container, c);

            struct xml_string* name = xml_node_name(track);
            uint8_t* name_str = calloc(xml_string_length(name) + 1, sizeof(uint8_t));
            xml_string_copy(name, name_str, xml_string_length(name));

            if (strcmp(name_str, "trkseg") == 0) {
                extract_path(track);
            }

            free(name_str);
    }
    printf("Path element count: %ld\n", path_len);

    // Calculate distance and difference in altitude between Waypoints
    // Calculate kms
    // Calculate time
    // Set pauses
    calculate_path_segments_data();
    
    // Output table
    // Output graph
    // Output latex doc
    // Create PDF

    return 0;
}