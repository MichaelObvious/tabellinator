#include <assert.h>
#include <float.h>
#include <locale.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "xml.c"

#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif

#define max(a,b) (((a)>(b))?(a):(b))

#define GRAPH_POINTS_COUNT 2500

#define TILE_WIDTH 17500
#define TILE_HEIGHT 12000

typedef struct {
    double e; // lv95
    double n;
    double ele; // m
    size_t idx;
} Point;

typedef struct {
    double dst; // km
    double dh; // m
    double kms; // kms
    uint64_t t; // minutes
    uint64_t pause; // minutes
} PathSegmentData;

typedef struct {
    double x, y;
} Vec2d;

double FACTOR = 5.0; // kms/h
double ADJUSTMENT_FACTOR = 1.2; // because of too much precision
uint64_t START_TIME =  0 * 60 + 0; // min

char out_file_path[128] = {0};

#define MAX_SOURCE_LEN 64 * 1000 * 1000
char source[MAX_SOURCE_LEN+1] = {0};
size_t source_size = 0;

char out_tex[MAX_SOURCE_LEN+1] = {0};
size_t tex_size = 0;

#define MAX_STR_SIZE 128
char name[MAX_STR_SIZE+1] = {0};

#define WAYPOINTS_CAPACITY 128
Point waypoints[WAYPOINTS_CAPACITY] = {0};
size_t waypoints_len = 0;

#define PAUSES_CAPACITY 128
uint64_t pauses[PAUSES_CAPACITY] = {0};
size_t pauses_len = 0;

#define PATH_CAPACITY 128 * 1000
Point path[PATH_CAPACITY] = {0};
size_t path_len = 0;

#define PATH_SEGMENTS_CAP WAYPOINTS_CAPACITY
PathSegmentData segments[PATH_SEGMENTS_CAP] = {0};
size_t segments_len = 0;

#define DIRECTIONS_COUNT 8
Vec2d directions_vectors[DIRECTIONS_COUNT] = {
    (Vec2d) { 1.0, 0.0 },
    (Vec2d) {0.7071067811865476, 0.7071067811865475},
    (Vec2d) {0.0, 1.0},
    (Vec2d) {-0.7071067811865475, 0.7071067811865476},
    (Vec2d) {-1.0, 0.0},
    (Vec2d) {-0.7071067811865477, -0.7071067811865475},
    (Vec2d) {0.0, -1.0},
    (Vec2d) {0.7071067811865474, -0.7071067811865477},
};
char* directions_labels[DIRECTIONS_COUNT] = {
    "west", "south west", "south", "south east", "east", "north east", "north", "north west"
};

Vec2d vec2d_invert(Vec2d a) {
    return (Vec2d) {
        -a.x,
        -a.y
    };
}

double vec2d_length(Vec2d a) {
    return sqrt(a.x*a.x + a.y*a.y);
}

Vec2d vec2d_normalized(Vec2d a) {
    double length = vec2d_length(a);
    if (length < 0.001) return (Vec2d) { 0.0, 0.0 };

    return (Vec2d) {
        a.x / length,
        a.y / length
    };
}

Vec2d vec2d_add(Vec2d a, Vec2d b) {
    return (Vec2d) {
        a.x + b.x,
        a.y + b.y
    };
}

double vec2d_dot(Vec2d a, Vec2d b) {
    return a.x * b.x + a.y * b.y;
}

void dsum_clear(double asum[2048]) {
    size_t i;
    for(i = 0; i < 2048; i++)
        asum[i] = 0.0;
}

void dsum_add(double asum[2048], double x) {
    size_t i;
    while (1) {
        /* i = exponent of f */
        i = ((size_t)((*(uint64_t *)&x)>>52))&0x7ffL;
        if (i == 0x7ffL){          /* max exponent, could be overflow */
            asum[i] += x;
            return;
        }
        if(asum[i] == 0.0){     /* if empty slot store f */
            asum[i] = x;
            return;
        }
        x += asum[i];           /* else add slot to f, clear slot */
        asum[i] = 0.0f;          /* and continue until empty slot */
    }
}

double dsum_return(double asum[2048]) {
    double sum = 0.f;
    size_t i;
    for(i = 0; i < 2048; i++)
        sum += asum[i];
    return sum;
}

inline static double map(const double n, const double nmin, const double nmax, const double min, const double max) {
    return ((n - nmin) * (max-min))/(nmax-nmin) + min;
}

// round to nearest 5 multiple
inline static double round5(const double x) {
    return round(x / 5.0) * 5.0;
}

inline static double deg2rad(const double a) {
    return (a / 180.0) * M_PI;
}

void wsg84_to_lv95(const double phi, const double lambda, double* E, double* N) {
    #define G2S(x) x * 3600.0
    double phi_ = (G2S(phi) - 169028.66)/10000.0;
    double lambda_ = (G2S(lambda) - 26782.5)/10000.0;

    *E = 2600072.37
        + 211455.93 * lambda_
        - 10938.51 * lambda_ * phi_
        - 0.36 * lambda_ * phi_ * phi_
        - 44.54 * lambda_ * lambda_ * lambda_;

    *N = 1200147.07
        + 308807.95 * phi_
        + 3745.25 * lambda_ * lambda_
        + 76.63 * phi_ * phi_
        - 194.56 * lambda_ * lambda_ * phi_
        + 119.79 * phi_ * phi_ * phi_;
}

void wsg84_to_lv95i(const double phi, const double lambda, uint64_t* E, uint64_t* N) {
    double e = 0, n = 0;
    wsg84_to_lv95(phi, lambda, &e, &n);
    *E = (uint64_t) round(e);
    *N = (uint64_t) round(n);
}

// km
double distance(const Point* a, const Point *b) {
    const double dE = a->e-b->e;
    const double dN = a->n-b->n;

    return sqrt(dE*dE + dN*dN)/1000.0;
}

// returns the actual length of the name
size_t waypoint_name(const size_t idx, char name[2]) {
    size_t length = 2;
    char letter = ((idx%26) + 'A');
    char number = (idx / 26) + '0';

    if (number == '0') {
        number = ' ';
        length = 1;
    }
    assert(number <= '9' && "Too many waypoints");
    name[0] = letter;
    name[1] = number;
    return length;
}

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

uint8_t* fix_source(uint8_t* src) {
    while (!(
        (*src        == (uint8_t) '<')
        && (*(src+1) == (uint8_t) 'g')
        && (*(src+2) == (uint8_t) 'p')
        && (*(src+3) == (uint8_t) 'x')
    ) && src < (uint8_t*) (source+MAX_SOURCE_LEN)) {
        src++;
    }
    // printf("%d", *src);
    return src;
}

void extract_tour_name(struct xml_node* root) {
    struct xml_node* metadata_node = xml_node_child(root, 0);
    struct xml_node* name_node = xml_node_child(metadata_node, 2);; //xml_easy_child(root, "name");
    struct xml_string* name_str = xml_node_content(name_node);

    assert(xml_string_length(name_str) + 1 <= MAX_STR_SIZE+1 && "Tour name too long.");
	xml_string_copy(name_str, (uint8_t*) name, xml_string_length(name_str));
}

void extract_waypoints(struct xml_node* root) {
    size_t children = xml_node_children(root);
    for (size_t i = 1; i < children-1; i++) { // first is metadata, last is track
        Point* wp = &waypoints[waypoints_len++];
        assert(waypoints_len <= WAYPOINTS_CAPACITY);

        struct xml_node* waypoint_node = xml_node_child(root, i);

        double lon, lat;
        for (size_t a = 0; a < xml_node_attributes(waypoint_node); a++) {
            struct xml_string* attr_name = xml_node_attribute_name(waypoint_node,a);
            uint8_t* attr_name_str = calloc(xml_string_length(attr_name) + 1, sizeof(uint8_t));
            xml_string_copy(attr_name, attr_name_str, xml_string_length(attr_name));

            if (strcmp((char*) attr_name_str, "lon")==0) {
                struct xml_string* attr_content = xml_node_attribute_content(waypoint_node,a);
                uint8_t* attr_content_str = calloc(xml_string_length(attr_content) + 1, sizeof(uint8_t));
                xml_string_copy(attr_content, attr_content_str, xml_string_length(attr_content));

                lon = atof((char*) attr_content_str);

                free(attr_content_str);
            } else if (strcmp((char*) attr_name_str, "lat")==0) {
                struct xml_string* attr_content = xml_node_attribute_content(waypoint_node,a);
                uint8_t* attr_content_str = calloc(xml_string_length(attr_content) + 1, sizeof(uint8_t));
                xml_string_copy(attr_content, attr_content_str, xml_string_length(attr_content));

                lat = atof((char*) attr_content_str);

                free(attr_content_str);
            }

            free(attr_name_str);
        }

        wsg84_to_lv95(lat, lon, &wp->e, &wp->n);

        size_t cdren = xml_node_children(waypoint_node);
        for (size_t c = 0; c < cdren; ++c) {
            struct xml_node* child = xml_node_child(waypoint_node, c);

            struct xml_string* name = xml_node_name(child);
            uint8_t* name_str = calloc(xml_string_length(name) + 1, sizeof(uint8_t));
            xml_string_copy(name, name_str, xml_string_length(name));

            if (strcmp((char*) name_str, "ele") == 0) {
                struct xml_string* content = xml_node_content(child);
                uint8_t* content_str = calloc(xml_string_length(content) + 1, sizeof(uint8_t));
                xml_string_copy(content, content_str, xml_string_length(content));

                wp->ele = atof((char*) content_str);

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


        double lon, lat;
        for (size_t a = 0; a < xml_node_attributes(point_node); a++) {
            struct xml_string* attr_name = xml_node_attribute_name(point_node,a);
            uint8_t* attr_name_str = calloc(xml_string_length(attr_name) + 1, sizeof(uint8_t));
            xml_string_copy(attr_name, attr_name_str, xml_string_length(attr_name));

            if (strcmp((char*) attr_name_str, "lon")==0) {
                struct xml_string* attr_content = xml_node_attribute_content(point_node,a);
                uint8_t* attr_content_str = calloc(xml_string_length(attr_content) + 1, sizeof(uint8_t));
                xml_string_copy(attr_content, attr_content_str, xml_string_length(attr_content));

                lon = atof((char*)attr_content_str);

                free(attr_content_str);
            } else if (strcmp((char*)attr_name_str, "lat")==0) {
                struct xml_string* attr_content = xml_node_attribute_content(point_node,a);
                uint8_t* attr_content_str = calloc(xml_string_length(attr_content) + 1, sizeof(uint8_t));
                xml_string_copy(attr_content, attr_content_str, xml_string_length(attr_content));

                lat = atof((char*)attr_content_str);

                free(attr_content_str);
            }

            free(attr_name_str);
        }

        wsg84_to_lv95(lat, lon, &p->e, &p->n);

        size_t cdren = xml_node_children(point_node);
        for (size_t c = 0; c < cdren; ++c) {
            struct xml_node* child = xml_node_child(point_node, c);

            struct xml_string* name = xml_node_name(child);
            uint8_t* name_str = calloc(xml_string_length(name) + 1, sizeof(uint8_t));
            xml_string_copy(name, name_str, xml_string_length(name));

            if (strcmp((char*) name_str, "ele") == 0) {
                struct xml_string* content = xml_node_content(child);
                uint8_t* content_str = calloc(xml_string_length(content) + 1, sizeof(uint8_t));
                xml_string_copy(content, content_str, xml_string_length(content));

                p->ele = atof((char*)content_str);

                free(content_str);
                free(name_str);
                break;
            }

            free(name_str);
        }

        // printf("    p-%d: %f %f %f\n", i, p->lat, p->lon, p->ele);
    }
}

double calculate_kms(double dst, double dh) {
    double pendenza = dh/dst;
    double kms = dst;
    if (pendenza > 0) {
        kms += dh / 100.0;
    } else if (pendenza < -0.2) {
        kms += -dh / 150.0;
    }
    return kms;
}

void calculate_path_segments_data() {
    assert(waypoints_len >= 2);
    waypoints[0].idx = 0;
    size_t wp_idx = 1;
    segments_len = 1;
    double dst_sum[2048] = {0};
    double dh_sum[2048] = {0};
    double kms_sum[2048] = {0};
    dsum_clear(dst_sum);
    dsum_clear(dh_sum);
    dsum_clear(kms_sum);

    for (size_t i = 1; i < path_len; i++) {
        assert(wp_idx < waypoints_len);
        PathSegmentData* ps = &segments[wp_idx-1];
        Point* pa = &path[i-1];
        Point* pb = &path[i];
        double dst = distance(pa, pb);
        double dh = pb->ele - pa->ele;
        double kms = calculate_kms(dst, dh);

        dsum_add(dst_sum, dst);
        dsum_add(dh_sum, dh);
        dsum_add(kms_sum, kms);

        if (distance(&waypoints[wp_idx], &path[i]) <= 0.0001) { // TODO: calculate min ddistance
            ps->dst = dsum_return(dst_sum);
            ps->dh = dsum_return(dh_sum);
            ps->kms = dsum_return(kms_sum);
            dsum_clear(dst_sum);
            dsum_clear(dh_sum);
            dsum_clear(kms_sum);
            double time = 60.0 * (ps->kms / (FACTOR * ADJUSTMENT_FACTOR));
            ps->t = (uint64_t) round(time);
            (ps+1)->pause = pauses[wp_idx];

            waypoints[wp_idx].idx = i;


            {
                // size_t min = ps->t % 60;
                // size_t hours = ps->t / 60;

                // uint64_t E = 0, N = 0;
                // wsg84_to_lv95i(waypoints[wp_idx].lat, waypoints[wp_idx].lon, &E, &N);
                // printf("Waypoint: %ld %ld\n", E, N);


                // printf("%f km; %f m; %f kms; %ld min (%02ldh %02ldm) - %ld\n", ps->dst, ps->dh, ps->kms, ps->t, hours, min, ps->pause);
            }
            wp_idx++;
            segments_len++;
        }
    }

    waypoints[waypoints_len-1].idx = path_len-1;
}

void parse_gpx(uint8_t* src, const char* file_path) {
    struct xml_document* document = xml_parse_document(src, strlen((char*) src));
    if (!document) {
		fprintf(stderr, "[ERROR] Could not parse file `%s`.\n", file_path);
		exit(EXIT_FAILURE);
	}

    struct xml_node* root = xml_document_root(document);

    size_t children = xml_node_children(root);

    // Retrieve tour name
    extract_tour_name(root);
    // printf("Tour name:         '%s'\n", name);

    // Retrieve Waypoints
    extract_waypoints(root);
    // printf("Numero di waypoints: %ld\n", waypoints_len);
    pauses_len = waypoints_len;

    char wp_name[2] = {0};
    for (size_t i = 1; i < waypoints_len-1; i++) {
        uint64_t hours = 0, mins = 0;
        size_t wp_name_len = waypoint_name(i, wp_name);

        printf(" - Pausa al punto '%.*s' (%ld/%ld) [hh:mm]: ", (int) wp_name_len, wp_name, i + 1, waypoints_len);
        scanf("%zu:%zu", &hours, &mins);

        if ((int64_t) hours >= 0 && (int64_t) mins >= 0)
            pauses[i] = hours * 60 + mins;
    }

    // Retrieve path
    struct xml_node* track_container = xml_node_child(root, children-1);
    size_t track_container_children = xml_node_children(track_container);
    for (size_t c = 0; c < track_container_children; ++c) {
        struct xml_node* track = xml_node_child(track_container, c);

            struct xml_string* name = xml_node_name(track);
            uint8_t* name_str = calloc(xml_string_length(name) + 1, sizeof(uint8_t));
            xml_string_copy(name, name_str, xml_string_length(name));

            if (strcmp((char*) name_str, "trkseg") == 0) {
                extract_path(track);
            }

            free(name_str);
    }
    // printf("Path element count: %ld\n", path_len);

    // Calculate distance and difference in altitude between Waypoints
    // Calculate kms
    // Calculate time
    // Set pauses
    calculate_path_segments_data();
}

uint64_t lv95_to_tileid(const uint64_t E, const uint64_t N) {
    uint64_t y = (1302000 - N - 1)/TILE_HEIGHT;
    uint64_t x = (E + 1 - 2480000)/TILE_WIDTH;
    return 1000 + y * 20 + x;
}

void tileid_coord(const uint64_t id, uint64_t* E, uint64_t* N) { // south west
    // uint64_t y = (1302000 - N)/TILE_HEIGHT;
    // uint64_t x = (E - 2480000)/TILE_WIDTH;
    uint64_t x = (id - 1000)%20;
    uint64_t y = (id - 1000)/20;
    *E = (x * TILE_WIDTH) + 2480000;
    *N = -((y * TILE_HEIGHT) - 1302000) - TILE_HEIGHT;
    // return 1000 + y * 20 + x;
}

uint64_t get_year(const uint64_t id) {
    switch (id) {case 1056:    return 1984;case 1035:    return 1988;case 1282:    return 1992;case 2220:    return 2003;case 1157:    return 2014;case 1159:    return 2014;case 1176:    return 2014;case 1177:    return 2014;case 1178:    return 2014;case 1179:    return 2014;case 1196:    return 2014;case 1197:    return 2014;case 1198:    return 2014;case 1199:    return 2014;case 2180:    return 2014;case 2200:    return 2014;case 1216:    return 2015;case 1217:    return 2015;case 1218:    return 2015;case 1219:    return 2015;case 1236:    return 2015;case 1237:    return 2015;case 1238:    return 2015;case 1239:    return 2015;case 1255:    return 2015;case 1256:    return 2015;case 1257:    return 2015;case 1258:    return 2015;case 1275:    return 2015;case 1276:    return 2015;case 1277:    return 2015;case 1278:    return 2015;case 1295:    return 2015;case 1296:    return 2015;case 1298:    return 2015;case 1318:    return 2015;case 1328:    return 2015;case 1329:    return 2015;case 1348:    return 2015;case 1349:    return 2015;case 1368:    return 2015;case 2240:    return 2015;case 2260:    return 2015;case 1166:    return 2016;case 1167:    return 2016;case 1187:    return 2016;case 1207:    return 2016;case 1213:    return 2016;case 1214:    return 2016;case 1215:    return 2016;case 1227:    return 2016;case 1230:    return 2016;case 1234:    return 2016;case 1235:    return 2016;case 1247:    return 2016;case 1263:    return 2016;case 1266:    return 2016;case 1267:    return 2016;case 1283:    return 2016;case 1286:    return 2016;case 1287:    return 2016;case 1303:    return 2016;case 1306:    return 2016;case 1307:    return 2016;case 1324:    return 2016;case 1325:    return 2016;case 1326:    return 2016;case 1327:    return 2016;case 1344:    return 2016;case 1345:    return 2016;case 1346:    return 2016;case 1347:    return 2016;case 1365:    return 2016;case 1366:    return 2016;case 1064:    return 2017;case 1065:    return 2017;case 1084:    return 2017;case 1085:    return 2017;case 1104:    return 2017;case 1105:    return 2017;case 1288:    return 2017;case 1289:    return 2017;case 1308:    return 2017;case 1309:    return 2017;case 1310:    return 2017;case 1047:    return 2018;case 1066:    return 2018;case 1067:    return 2018;case 1086:    return 2018;case 1087:    return 2018;case 1106:    return 2018;case 1126:    return 2018;case 1127:    return 2018;case 1146:    return 2018;case 1147:    return 2018;case 1208:    return 2018;case 1228:    return 2018;case 1229:    return 2018;case 1248:    return 2018;case 1249:    return 2018;case 1250:    return 2018;case 1268:    return 2018;case 1269:    return 2018;case 1270:    return 2018;case 1290:    return 2018;case 1011:    return 2019;case 1012:    return 2019;case 1031:    return 2019;case 1032:    return 2019;case 1033:    return 2019;case 1034:    return 2019;case 1052:    return 2019;case 1053:    return 2019;case 1054:    return 2019;case 1055:    return 2019;case 1071:    return 2019;case 1072:    return 2019;case 1073:    return 2019;case 1074:    return 2019;case 1075:    return 2019;case 1076:    return 2019;case 1092:    return 2019;case 1093:    return 2019;case 1094:    return 2019;case 1095:    return 2019;case 1096:    return 2019;case 1112:    return 2019;case 1113:    return 2019;case 1114:    return 2019;case 1115:    return 2019;case 1116:    return 2019;case 1132:    return 2019;case 1133:    return 2019;case 1134:    return 2019;case 1135:    return 2019;case 1136:    return 2019;case 1152:    return 2019;case 1153:    return 2019;case 1154:    return 2019;case 1155:    return 2019;case 1156:    return 2019;case 1174:    return 2019;case 1175:    return 2019;case 1194:    return 2019;case 1195:    return 2019;case 1123:    return 2020;case 1124:    return 2020;case 1125:    return 2020;case 1143:    return 2020;case 1144:    return 2020;case 1145:    return 2020;case 1162:    return 2020;case 1163:    return 2020;case 1164:    return 2020;case 1165:    return 2020;case 1182:    return 2020;case 1183:    return 2020;case 1184:    return 2020;case 1185:    return 2020;case 1186:    return 2020;case 1201:    return 2020;case 1202:    return 2020;case 1203:    return 2020;case 1204:    return 2020;case 1205:    return 2020;case 1206:    return 2020;case 1221:    return 2020;case 1222:    return 2020;case 1223:    return 2020;case 1224:    return 2020;case 1225:    return 2020;case 1226:    return 2020;case 1240:    return 2020;case 1241:    return 2020;case 1242:    return 2020;case 1243:    return 2020;case 1244:    return 2020;case 1245:    return 2020;case 1246:    return 2020;case 1260:    return 2020;case 1261:    return 2020;case 1262:    return 2020;case 1264:    return 2020;case 1265:    return 2020;case 1280:    return 2020;case 1281:    return 2020;case 1284:    return 2020;case 1285:    return 2020;case 1300:    return 2020;case 1301:    return 2020;case 1304:    return 2020;case 1305:    return 2020;case 1320:    return 2020;case 1048:    return 2021;case 1049:    return 2021;case 1050:    return 2021;case 1051:    return 2021;case 1068:    return 2021;case 1069:    return 2021;case 1070:    return 2021;case 1088:    return 2021;case 1089:    return 2021;case 1090:    return 2021;case 1091:    return 2021;case 1107:    return 2021;case 1108:    return 2021;case 1109:    return 2021;case 1110:    return 2021;case 1111:    return 2021;case 1128:    return 2021;case 1129:    return 2021;case 1130:    return 2021;case 1131:    return 2021;case 1148:    return 2021;case 1149:    return 2021;case 1150:    return 2021;case 1151:    return 2021;case 1168:    return 2021;case 1169:    return 2021;case 1170:    return 2021;case 1171:    return 2021;case 1172:    return 2021;case 1173:    return 2021;case 1188:    return 2021;case 1189:    return 2021;case 1190:    return 2021;case 1191:    return 2021;case 1192:    return 2021;case 1193:    return 2021;case 1209:    return 2021;case 1210:    return 2021;case 1211:    return 2021;case 1212:    return 2021;case 1231:    return 2021;case 1232:    return 2021;case 1233:    return 2021;case 1251:    return 2021;case 1252:    return 2021;case 1253:    return 2021;case 1254:    return 2021;case 1271:    return 2021;case 1272:    return 2021;case 1273:    return 2021;case 1274:    return 2021;case 1291:    return 2021;case 1292:    return 2021;case 1293:    return 2021;case 1294:    return 2021;case 1311:    return 2021;case 1312:    return 2021;case 1313:    return 2021;case 1314:    return 2021;case 1332:    return 2021;case 1333:    return 2021;case 1334:    return 2021;case 1352:    return 2021;case 1353:    return 2021;case 1354:    return 2021;case 1373:    return 2021;case 1374:    return 2021;default:    assert(0 && "unreachable");}
}

// UNFINISHED
void print_map(FILE* sink) {
    fprintf(sink, "\n");
    fprintf(sink, "\\pagebreak\n");
    fprintf(sink, "\n");

    fprintf(sink, "    \\begin{center}\n");
    fprintf(sink, "        \\textsc{\\Large %s}\n", name);
    fprintf(sink, "\n");
    fprintf(sink, "        \\vspace{0.5ex}\n");
    fprintf(sink, "\n");
    fprintf(sink, "\\textsc{\\small Carta topografica}\n");
    fprintf(sink, "    \\end{center}\n");

    fprintf(sink, "\n");
    fprintf(sink, "\\vspace{2ex}\n");
    fprintf(sink, "\n");

    {
        uint64_t minE = 1000000000, maxE = 0, minN = 1000000000, maxN = 0;
        uint64_t E = 0, N = 0;
        for (size_t i = 0; i < path_len; i++) {
            E = path[i].e;
            N = path[i].n;
            if (E < minE) minE = E;
            if (E > maxE) maxE = E;
            if (N < minN) minN = N;
            if (N > maxN) maxN = N;
        }

        uint64_t width = (maxE - minE);
        uint64_t height = (maxN - minN);


        uint64_t new_height = height, new_width = width;
        const double rel_width = 34.6;
        const double rel_height = 21.75;
        const double rel_ratio = rel_width / rel_height;
        if (width > height) {
            if((double) width / (double) height > rel_ratio) {
                new_height = width * rel_height / rel_width;
                minN -= (new_height - height)/2;
                maxN += (new_height - height)/2;
            } else if ((double) width / (double) height < rel_ratio) {
                new_width = height * rel_width / rel_height;
                minE -= (new_width - width) /2;
                maxE += (new_width - width)/2;
            }
        } else {
            if((double) height / (double) width > rel_ratio) {
                new_width = height * rel_height / rel_width;
                minE -= (new_width - width) / 2;
                maxE += (new_width - width) / 2;
            } else if ((double) height / (double) width < rel_ratio) {
                new_height = width * rel_width / rel_height;
                minN -= (new_height - height) / 2;
                maxN += (new_height - height) / 2;
            }
        }

        width = new_width;
        height = new_height;

        width = width * 12 / 10;
        height = height * 12 / 10;

        minE -= width / 12;
        maxE += width / 12;
        minN -= height / 12;
        maxN += height / 12;

        // printf("MIN %ld %ld\n", minE, minN);
        // printf("MAX %ld %ld\n", maxE, maxN);

        // double scalex = (double) TILE_WIDTH / (double) width;
        // double scaley = (double) TILE_HEIGHT / (double) height;

        // double scale = (scalex < scaley ? scalex : scaley);
        // if (scale < 1.0) {
        //     scale = 1.0;
        // }

        const double cell_size = 0.8;
        fprintf(sink, "\\begin{center}\n\\begin{tikzpicture}[x=%lfcm,y=%lfcm, step=%lfcm", cell_size, cell_size, cell_size);
        if (height > width) {
            fprintf(sink, ", rotate=270, transform shape");
        }
        fprintf(sink, "] \n");

        double max_size = 0.0;
        if (height < width) {
            max_size = (double) rel_height;
            if (max_size / height * width > (double) rel_width) {
                max_size = rel_width / width * height;
            }
        } else {
            max_size = (double) rel_width;
            if (max_size / height * width > (double) rel_height) {
                max_size = (double) rel_width / width * height;
            }
        }

        double scale = (double) height / (max_size * cell_size * 0.01);
        printf("Map scale: 1:%ld\n", (uint64_t) round(scale));
        // fprintf(sink, "\\draw[very thin,color=black!10] (0.0,0.0) grid (%.1lf,-%.1lf);\n", 30.0+0.5, 20.0+0.5);

        const uint64_t dimension = width > height ? width : height;
        double resolution_kinda = round(log2((double)dimension / TILE_WIDTH + 1.0));
        resolution_kinda = resolution_kinda < 0.0 ? 0.0 : resolution_kinda;

        uint64_t resolution_id = (uint64_t) resolution_kinda < 5 ? (uint64_t) resolution_kinda : 5;

        uint64_t full_image_size_x = 0, full_image_size_y = 0;

        switch (resolution_id)
        {
        case 0:
            full_image_size_x = 14000;
            full_image_size_y = 9600;
            break;
        case 1:
            full_image_size_x = 7000;
            full_image_size_y = 4800;
            break;
        case 2:
            full_image_size_x = 3500;
            full_image_size_y = 2400;
            break;
        case 3:
            full_image_size_x = 1750;
            full_image_size_y = 1200;
            break;
        case 4:
            full_image_size_x = 875;
            full_image_size_y = 600;
            break;
        case 5:
            full_image_size_x = 438;
            full_image_size_y = 300;
            break;
        default:
            assert(0 && "Unreacheable");
            break;
        }

        printf("[INFO] Chosen resolution: %ld (%ldx%ldpx)\n", resolution_id, full_image_size_x, full_image_size_y);

        size_t t = time(NULL);

        size_t checked_ids_size = 0;
        uint64_t checked_ids[32] = {0};
        size_t frame_id = 0;

        const uint64_t min_dimension = width < height ? width : height;
        uint64_t step = min_dimension / 32;
        step = step < 100 ? step : 100;

        for (uint64_t e = minE; e <= maxE; e += step) {
            for (uint64_t n = maxN; n >= minN; n -= step) {
                uint64_t id = lv95_to_tileid(e, n);
                uint64_t e2 = 0, n2 = 0;
                tileid_coord(id, &e2, &n2);
                // printf("%ld %ld -> %ld %ld\n", e, n, e2, n2);
                // printf("    %ld %ld -> %ld %ld\n", e - e2, n - n2, TILE_WIDTH, TILE_HEIGHT);
                // printf("    %ld %ld\n", id, lv95_to_tileid(e2+1, n2+1));

                int checked = 0;
                for (size_t i = 0; i < checked_ids_size; i++) {
                    if (checked_ids[i] == id) checked = 1;
                }
                if (checked) continue;
                checked_ids[checked_ids_size++] = id;

                char tiff_file[16] = {0};
                char jpg_file[16] = {0};
                char map_file[24] = {0};
                snprintf(tiff_file, 15, "%ld.tif", id);
                snprintf(jpg_file, 15, "%ld-%ld.jpg", id, resolution_id);
                snprintf(map_file, 24, "map-%ld-%05ld.jpg", id, t%100000);

                uint64_t year = get_year(id);

                if (!file_exists(tiff_file)) {
                    char get_url_cmd[256] = {0};
                    snprintf(get_url_cmd, 255, "curl https://data.geo.admin.ch/ch.swisstopo.pixelkarte-farbe-pk25.noscale/swiss-map-raster25_%ld_%ld/swiss-map-raster25_%ld_%ld_krel_1.25_2056.tif > %s"
    #ifdef _WIN32
                        " > nul"
    #else
                        " 2> /dev/null"
    #endif
                    , year, id, year, id, tiff_file);
                    printf("[INFO] Donwloading map [id=%ld]... ", id);
                    fflush(stdout);
                    system(get_url_cmd);
                    printf("done!\n");
                }

                if (!file_exists(jpg_file)) {

                    char convert_cmd[256] = {0};
                    snprintf(convert_cmd, 255, "magick %s %.*s.jpg"
    #ifdef _WIN32
                        " > nul"
    #else
                        " 2> /dev/null"
    #endif
                    , tiff_file, (int) strlen(tiff_file)-4, tiff_file);
                    printf("[INFO] Converting map  [id=%ld]... ", id);
                    fflush(stdout);
                    system(convert_cmd);
                    printf("done!\n");
                }

                // get the coordinates contained in the map
                int64_t mapMinE = 0, mapMinN = 0;
                int64_t mapMaxE = 0, mapMaxN = 0;

                tileid_coord(id, (uint64_t*) &mapMinE, (uint64_t*) &mapMinN);
                mapMaxE = mapMinE + TILE_WIDTH;
                mapMaxN = mapMinN + TILE_HEIGHT;
                // printf("MAP: %ld %ld %ld %ld\n", mapMinE, mapMinN, mapMaxE, mapMaxN);
                // printf("MINIMAP: %ld %ld %ld %ld\n", minE, minN, maxE, maxN);

                int64_t min_contained_E = 0, min_contained_N = 0;
                int64_t max_contained_E = 0, max_contained_N = 0;

                min_contained_E = mapMinE > (int64_t) minE ? (int64_t) mapMinE : (int64_t) minE;
                max_contained_E = mapMaxE < (int64_t) maxE ? (int64_t) mapMaxE : (int64_t) maxE;
                min_contained_N = mapMinN > (int64_t) minN ? (int64_t) mapMinN : (int64_t) minN;
                max_contained_N = mapMaxN < (int64_t) maxN ? (int64_t) mapMaxN : (int64_t) maxN;
                // printf("MIN MIN / MAX MAX: %ld %ld %ld %ld\n", mapMinE, mapMinN, mapMaxE, mapMaxN);
                // printf("MIN MIN / MAX MAX: %ld %ld %ld %ld\n", min_contained_E, min_contained_N, max_contained_E, max_contained_N);

                // get the size of the cropped image in pixels
                double cropped_map_width = (double) (max_contained_E - min_contained_E) / (double) TILE_WIDTH;
                double cropped_map_height = (double) (max_contained_N - min_contained_N) /  (double) TILE_HEIGHT;
                uint64_t cropped_image_width =  (uint64_t) ((double) full_image_size_x * cropped_map_width);
                uint64_t cropped_image_height = (uint64_t) ((double) full_image_size_y * cropped_map_height);
                // printf("WIDTH / HEIGHT: %ld %ld\n", cropped_image_width, cropped_image_height);

                if (cropped_image_width == 0 || cropped_image_height == 0) continue;

                // get the offset from the center of the image
                double coord_offset_x = (double) (min_contained_E - mapMinE) / (double) TILE_WIDTH;
                double coord_offset_y = (double) (mapMaxN - max_contained_N) / (double) TILE_HEIGHT;
                // printf("COORD OFFSETS: %f %f\n", coord_offset_x * TILE_WIDTH, coord_offset_y * TILE_HEIGHT);
                
                int64_t pixel_offset_x = (int64_t) (coord_offset_x * (double)full_image_size_x);
                int64_t pixel_offset_y = (int64_t)  (coord_offset_y * (double)full_image_size_y);
                // printf("PX OFFSETS: %ld %ld\n", pixel_offset_x, pixel_offset_y);
                // printf("\n");

                //construct command
                char crop_cmd[256] = {0};
                snprintf(crop_cmd, 256, "magick %s -crop %ldx%ld%+ld%+ld %s", jpg_file, cropped_image_width, cropped_image_height, pixel_offset_x, pixel_offset_y, map_file);
                // printf("[CROP] %s\n", crop_cmd);
                printf("[INFO] Cropping map    [id=%ld]... ", id);
                fflush(stdout);
                system(crop_cmd);
                printf("done!\n");
                // call command

                // assert images are only one
                // put image in latex

                // find coordinates in page
                uint64_t center_N = (max_contained_N + min_contained_N) / 2;
                uint64_t center_E = (max_contained_E + min_contained_E) / 2;
                // double ax = ((min_contained_E - (double) minE) / (double) height) * 20.0;
                // double ay = (((double) maxN - max_contained_N) / (double) height) * 20.0;
                // double bx = ((max_contained_E - (double) minE) / (double) height) * 20.0;
                // double by = (((double) maxN - min_contained_N) / (double) height) * 20.0;
                // printf("1: %lf %lf %lf %lf", (double) minE, (double) minE+height, 0.0, max_size);
                double x = map(center_E, minE, minE + height, 0.0, max_size);
                double y = map(center_N, maxN, minN, 0.0, max_size);

                // find dimensions
                double w = (((double) (max_contained_E - min_contained_E) / (double) height)) * cell_size * max_size;
                double h = (((double) (max_contained_N - min_contained_N) / (double) height)) * cell_size * max_size;
                
                fprintf(sink, "\\node[inner sep=0pt] (russel) at (%lf, -%lf) {\\includegraphics[width=%lfcm, height=%lfcm]{%s}};\n", x, y, w, h, map_file);
                // fprintf(sink, "\\filldraw[black] (%lf, -%lf) circle (2pt) node[anchor=south west]{%s};\n", x, y, map_file);

                frame_id += 1;

                // draw path
                // draw waypoints

                // printf("%s\n", url);
            }
        }

        fprintf(sink, "\\begin{scope}[transparency group, opacity=0.50]\n");

        fprintf(sink, "\\draw[red, line width=1.5pt] plot[smooth] coordinates{");
        // printf("2: %lf %lf %lf %lf", (double) minE, (double) minE+height, 0.0, max_size);
        size_t index_step = max(path_len / GRAPH_POINTS_COUNT, 1);
        for (size_t i = 0; i < path_len; i ++) {
            if (i % index_step == 0 || i == path_len - 1)
                fprintf(sink, "(%lf, %lf) ",
                    map(path[i].e, minE, minE+height, 0.0, max_size),
                    map(path[i].n, maxN, minN, 0.0, -max_size));
        }
        fprintf(sink, "};\n");
        for (size_t i = 0; i < waypoints_len; i++) {
            fprintf(sink, "\\filldraw[red] (%lf,%lf) circle (2.25pt);\n",
                map(waypoints[i].e, minE, minE+height, 0.0, max_size),
                map(waypoints[i].n, maxN, minN, 0.0, -max_size));

        }

        fprintf(sink, "\\end{scope}\n");

        // fprintf(sink, "\\begin{scope}[transparency group, opacity=1.0]\n");

        char wp_name[2] = {0};
        for (size_t i = 0; i < waypoints_len; i++) {
            size_t wp_name_len = waypoint_name(i, wp_name);
            // printf("%.*s (%ld/%ld)\n", 2, wp_name, waypoints[i].idx, path_len);
            // TODO: anchor based on path (calculate the angle before and angle after, take average, opposite shall be the letter)

            const int64_t step = 1;
            const int64_t max_samples = 25;
            
            Vec2d direction_vec = {0};
            char* direction_str = NULL;
            Vec2d forward_vec = {0};
            Vec2d backward_vec = {0};
            
            size_t path_idx = waypoints[i].idx;
            // printf("WAYPOINT PATH IDX: %ld\n", waypoints[i].idx);

            uint64_t x = waypoints[i].e, y = waypoints[i].n;
            double sum_x = 0.0, sum_y = 0.0;
            int64_t n = 0, count = 0;
            for (int64_t j = path_idx-1; j > (int64_t) path_idx - step * max_samples && j >= 0; j--) {
                // printf("    (%ld) %lf %lf\n", j, sum_x, sum_y);
                sum_x += (path[j].e - x) * (max_samples - n + 1);
                sum_y += (path[j].n - y) * (max_samples - n + 1);
                n ++;
                count += max_samples - count + 1;
            }
            // printf("BACKWARD COUNT: %ld\n", count);
            count = count <= 0 ? 1 : count;
            backward_vec = (Vec2d) {
                sum_x / (double) count, sum_y / (double) count
            };
            backward_vec = vec2d_normalized(backward_vec);
            // printf("BACKWARD %lf %lf\n", backward_vec.x, backward_vec.y);

            sum_x = 0.0; sum_y = 0.0; count = 0; n = 0;
            for (int64_t j = path_idx+1; j < path_idx + step * max_samples && j < path_len; j++) {
                sum_x += (path[j].e - x) * (max_samples - n + 1);
                sum_y += (path[j].n - y) * (max_samples - n + 1);
                n ++;
                count += max_samples - count + 1;
            }
            // printf("FORWARD COUNT: %ld\n", count);
            count = count <= 0 ? 1 : count;
            forward_vec = (Vec2d) {
                sum_x / (double) count, sum_y / (double) count
            };
            forward_vec = vec2d_normalized(forward_vec);
            // printf("FORWARD %lf %lf\n", forward_vec.x, forward_vec.y);

            if (vec2d_length(forward_vec) < 0.001) {
                forward_vec = backward_vec;
                // printf("FORWARD IS SHORT\n");
            }
            if (vec2d_length(backward_vec) < 0.001) {
                backward_vec = forward_vec;
                // printf("BACKWARD IS SHORT\n");
            }

            if (vec2d_dot(forward_vec, backward_vec) < -1.0 + 0.001) {
                // printf("Very opposite! %lf\n", vec2d_dot(forward_vec, backward_vec));
                double center_x = (maxE - minE)/2.0;
                double center_y = (maxN - minN)/2.0;
                Vec2d ray_from_center = vec2d_normalized((Vec2d) {
                    x - center_x,
                    y - center_y,
                });
                direction_vec = (Vec2d) {
                    -forward_vec.y,
                    forward_vec.x,
                };
                if (vec2d_dot(direction_vec, ray_from_center) < 0.0) {
                    direction_vec = vec2d_invert(direction_vec);
                }
            } else {
                direction_vec = vec2d_invert(vec2d_add(forward_vec, backward_vec));
            }

            direction_vec = vec2d_normalized(direction_vec);
            for (size_t k = 0; k < DIRECTIONS_COUNT; k++) {
                // printf("DIFFERENCE OF DISTANCE: %lf\n", vec2d_dot(directions_vectors[k], direction_vec) - 0.923);
                if (vec2d_dot(directions_vectors[k], direction_vec) > 0.923) {
                    direction_str = directions_labels[k];
                    // fprintf(sink, "\\draw[green] [->, ultra thick] (%lf, %lf) -- (%lf, %lf);\n",
                    //     map(waypoints[i].e, minE, minE+height, 0.0, max_size),
                    //     map(waypoints[i].n, maxN, minN, 0.0, -max_size),
                    //     map(waypoints[i].e, minE, minE+height, 0.0, max_size) + directions_vectors[k].x,
                    //     map(waypoints[i].n, maxN, minN, 0.0, -max_size) + directions_vectors[k].y);
                    break;
                }
            }
            assert(direction_str != NULL);
            
            // printf("DIRECTION %lf %lf, `%s`\n", direction_vec.x, direction_vec.y, direction_str);

            fprintf(sink, "\\filldraw[red!90!black, fill opacity=0.0, draw opacity=0.0, text opacity=1.0] (%lf,%lf) circle (2.25pt) node[anchor=%s, inner sep=2.5mm]{\\textbf{\\contour{white}{\\small %.*s}}};\n",
                map(waypoints[i].e, minE, minE+height, 0.0, max_size),
                map(waypoints[i].n, maxN, minN, 0.0, -max_size),
                direction_str,
                wp_name_len, wp_name);

            // fprintf(sink, "\\draw[black] [->, ultra thick] (%lf, %lf) -- (%lf, %lf);\n",
            //     map(waypoints[i].e, minE, minE+height, 0.0, max_size),
            //     map(waypoints[i].n, maxN, minN, 0.0, -max_size),
            //     map(waypoints[i].e, minE, minE+height, 0.0, max_size) + backward_vec.x,
            //     map(waypoints[i].n, maxN, minN, 0.0, -max_size) + backward_vec.y);
            // fprintf(sink, "\\draw[blue] [->, ultra thick] (%lf, %lf) -- (%lf, %lf);\n",
            //     map(waypoints[i].e, minE, minE+height, 0.0, max_size),
            //     map(waypoints[i].n, maxN, minN, 0.0, -max_size),
            //     map(waypoints[i].e, minE, minE+height, 0.0, max_size) + forward_vec.x,
            //     map(waypoints[i].n, maxN, minN, 0.0, -max_size) + forward_vec.y);
            // fprintf(sink, "\\draw[orange] [->, ultra thick] (%lf, %lf) -- (%lf, %lf);\n",
            //     map(waypoints[i].e, minE, minE+height, 0.0, max_size),
            //     map(waypoints[i].n, maxN, minN, 0.0, -max_size),
            //     map(waypoints[i].e, minE, minE+height, 0.0, max_size) + direction_vec.x,
            //     map(waypoints[i].n, maxN, minN, 0.0, -max_size) + direction_vec.y);

        }

        fprintf(sink, "\\draw[black] (%lf, %lf) rectangle (%lf, %lf);\n",
            map(minE, minE, minE+height, 0.0, max_size), map(minN, maxN, minN, 0.0, -max_size),
            map(maxE, minE, minE+height, 0.0, max_size), map(maxN, maxN, minN, 0.0, -max_size)
        );

        // NORTH ARROW
        // fprintf(sink, "\\draw [-stealth, ultra thick, white]  (%lf,%lf) -- (%lf,%lf);\n",
        //     map(maxE, minE, height+minE, 0, max_size) - 1.5,
        //     map(maxN, maxN, minN, 0, -max_size) - 1.5,
        //     map(maxE, minE, height+minE, 0, max_size) - 1.5,
        //     map(maxN, maxN, minN, 0, -max_size) - 0.5);
        // fprintf(sink, "\\draw [-stealth, thick, black]  (%lf,%lf) -- (%lf,%lf);\n",
        //     map(maxE, minE, height+minE, 0, max_size) - 1.5,
        //     map(maxN, maxN, minN, 0, -max_size) - 1.45,
        //     map(maxE, minE, height+minE, 0, max_size) - 1.5,
        //     map(maxN, maxN, minN, 0, -max_size) - 0.55);
        // fprintf(sink, "\\filldraw[red] (%lf,%lf) circle (8pt) node[anchor=south west]{%s};\n", 0.0, 0.0, "B");
        // fprintf(sink, "\\filldraw[red] (%lf,%lf) circle (2pt) node[anchor=south west]{%s};\n", 0.0, -20.0, "C");
        // fprintf(sink, "\\filldraw[red] (%lf,%lf) circle (2pt) node[anchor=south west]{%s};\n", 20.0, -20.0, "D");
        //  fprintf(sink, "\\end{scope}\n");

        fprintf(sink, "\\end{tikzpicture}\n");
        fprintf(sink, "\\end{center}\n");
    }

}

void print_latex_document(FILE* sink, int include_map) {
    #define DOC_MARGIN 1.0
    setlocale(LC_NUMERIC, "");

    fprintf(sink, "\\documentclass[a4paper,10pt,landscape]{article}\n");
    fprintf(sink, "\\usepackage[outline,copies]{contour}\n");
    fprintf(sink, "\\usepackage{multirow}\n");
    fprintf(sink, "\\usepackage[margin=%.0fcm]{geometry}\n", DOC_MARGIN);
    fprintf(sink, "\\usepackage{microtype}\n");
    fprintf(sink, "\\usepackage{longtable}\n");
    fprintf(sink, "\\usepackage{graphicx}\n");
    fprintf(sink, "\\usepackage{tikz}\n");
    fprintf(sink, "\\usepgflibrary{plotmarks}\n");
    fprintf(sink, "\\usepgflibrary{shapes.geometric}\n");
    fprintf(sink, "\\usetikzlibrary{positioning}\n");

    fprintf(sink, "\n");
    fprintf(sink, "\\begin{document}\n");
    fprintf(sink, "    \\pagenumbering{gobble}\n");
    fprintf(sink, "    \\contourlength{0.5pt}\n");
    fprintf(sink, "    \\contournumber{16}\n");
    fprintf(sink, "    \\begin{center}\n");
    fprintf(sink, "        \\textsc{\\Huge %s}\n", name);
    fprintf(sink, "\n");
    fprintf(sink, "        \\vspace{2ex}\n");
    fprintf(sink, "\n");
    fprintf(sink, "\\textsc{\\large Tabella di marcia}\n");
    fprintf(sink, "    \\end{center}\n");
    fprintf(sink, "\n");
    fprintf(sink, "\\vspace{2ex}\n");
    fprintf(sink, "\n");
    fprintf(sink, "    \\begin{center}\\begin{tabular}{|c|c|}\n");
    fprintf(sink, "        \\hline\n");
    fprintf(sink, "        \\multirow{2}{*}{Fattore di marcia:} & \\multirow{2}{*}{$%0.1f \\hphantom{a} \\frac{kms}{h}$}\\\\\n", FACTOR);
    fprintf(sink, "        &\\\\\n");
    fprintf(sink, "        \\hline\n");
    fprintf(sink, "    \\end{tabular}\\end{center}\n");
    fprintf(sink, "\n");
    fprintf(sink, "    \\begin{longtable}{|c|c|c|c|c|c|c|c|c|c|c|c|c|l|}\n");
    fprintf(sink, "        \\hline\n");
    fprintf(sink, "        \\multirow{2}{*}{Nome} & \\multirow{2}{*}{Coord. (LV95)} & \\multirow{2}{*}{Alt. [m]} & \\multirow{2}{*}{$\\Delta h$ [hm]} & \\multirow{2}{*}{$\\Delta s$ [km]} & \\multirow{2}{*}{$\\Delta kms$} & \\multirow{2}{*}{$\\Delta t$ [hh:mm]} & \\multirow{2}{*}{$s$ [km]} & \\multirow{2}{*}{$kms$} & \\multirow{2}{*}{$t$ [hh:mm]} & \\multirow{2}{*}{$t$ [hh:mm]} & \\multirow{2}{*}{Pausa [hh:mm]} & \\multirow{2}{*}{Osservazioni \\hphantom{aaaaaaaaaa}} \\\\\n");
    fprintf(sink, "        &&&&&&&&&&&&\\\\\n");
    fprintf(sink, "        \\hline\n");
    fprintf(sink, "        \\hline\n");

    char wp_name[2] = "  ";
    double km = 0, kms = 0;
    uint64_t t = START_TIME;
    // assert(waypoints_len == segments_len + 1);
    for (size_t i = 0; i < waypoints_len; i++) {
        Point wp = waypoints[i];
        PathSegmentData psd = segments[i];
        waypoint_name(i, wp_name);
        fprintf(sink, "\\multirow{2}{*}{%.*s} & ", 2, wp_name);
        fprintf(sink, "\\multirow{2}{*}{%'ld %'ld} & ", (uint64_t) round(wp.e), (uint64_t) round(wp.n));
        fprintf(sink, "\\multirow{2}{*}{%'.0f} & ", round(wp.ele));
        fprintf(sink, " & & & & ");
        fprintf(sink, "\\multirow{2}{*}{%.1f} &", km);
        fprintf(sink, "\\multirow{2}{*}{%.1f} &", kms);
        fprintf(sink, "\\multirow{2}{*}{%02ld:%02ld} &", (t / 60)%24, t % 60);
        fprintf(sink, "\\multirow{2}{*}{} &");
        if (i < waypoints_len-1 && i > 0 && psd.pause > 0) {
            fprintf(sink, "\\multirow{2}{*}{%02ld:%02ld} &", psd.pause / 60, psd.pause % 60);
        } else {
            fprintf(sink, "\\multirow{2}{*}{} &");
        }
        fprintf(sink, "\\multirow{2}{*}{}\\\\\n");
        fprintf(sink, "        \\cline{4-7} \n");
        if (i < waypoints_len-1) {
            fprintf(sink, " & & & ");
            fprintf(sink, "\\multirow{2}{*}{%.1f} & ", psd.dh/100.0);
            fprintf(sink, " \\multirow{2}{*}{%.1f} &", psd.dst);
            fprintf(sink, " \\multirow{2}{*}{%.1f} &", psd.kms);
            fprintf(sink, "\\multirow{2}{*}{%02ld:%02ld}&&&&&& \\\\\n", psd.t / 60, psd.t % 60);

            km += psd.dst;
            kms += psd.kms;
            t += psd.t + psd.pause;

            // fprintf(sink, "&&&&&&&&& \\\\\n");
            fprintf(sink, "        \\cline{1-3}\\cline{8-13} \n");
        } else {
            fprintf(sink, "&&&&&&&&&&&&\\\\\n");
            fprintf(sink, "\\hline\n");
        }
    }

    fprintf(sink, "    \\end{longtable}\n");

    fprintf(sink, "\n");
    fprintf(sink, "\n");
    fprintf(sink, "        \\vspace{2ex}\n");
    fprintf(sink, "\n");

    fprintf(sink, "    \\begin{center}\\begin{tabular}{|c|c|c|c|c|c|c|}\n");
    fprintf(sink, "        \\hline\n");
    fprintf(sink, "        \\multicolumn{2}{|c|}{\\multirow{2}{*}{Estremi}} & \\multicolumn{5}{|c|}{\\multirow{2}{*}{Totali}}\\\\\n");
    fprintf(sink, "        \\multicolumn{2}{|c|}{} & \\multicolumn{5}{|c|}{} \\\\\n");
    fprintf(sink, "        \\hline\n");
    fprintf(sink, "        \\multirow{2}{*}{$\\min h$} & \\multirow{2}{*}{$\\max h$} & \\multirow{2}{*}{$\\Delta h^\\uparrow$} & \\multirow{2}{*}{$\\Delta h^\\downarrow$} & \\multirow{2}{*}{$s$} & \\multirow{2}{*}{$kms$} & \\multirow{2}{*}{$t$ (senza pause)} \\\\\n");
    fprintf(sink, "        &&&&&& \\\\\n");
    fprintf(sink, "        \\hline\n");

    double minh = 3000, maxh = 0;
    double updh_sum[2048] = {0};
    double downdh_sum[2048] = {0};
    dsum_clear(updh_sum);
    dsum_clear(downdh_sum);
    for (size_t i = 0; i < path_len; i++) {
        Point p = path[i];
        if (p.ele < minh) minh = p.ele;
        if (p.ele > maxh) maxh = p.ele;
        if (i > 0) {
            Point p_ = path[i-1];
            double dh = p.ele - p_.ele;
            if (dh > 0.0) dsum_add(updh_sum, dh); else dsum_add(downdh_sum, -dh);
        }
    }
    // printf("%lf\n", dsum_return(updh_sum)-dsum_return(downdh_sum) - maxh + minh);

    // // DIRTY HACK
    // double counting_error = dsum_return(updh_sum)-dsum_return(downdh_sum) - maxh + minh;
    // if (counting_error < 0.0) {
    //     dsum_add(downdh_sum, counting_error);
    // } else {
    //     dsum_add(updh_sum, -counting_error);
    // }
    // printf("%lf\n", dsum_return(updh_sum)-dsum_return(downdh_sum) - maxh + minh);

    uint64_t tot_time = (uint64_t) round(60.0 * kms / (FACTOR * ADJUSTMENT_FACTOR));
    fprintf(sink, "        \\multirow{2}{*}{%.0f m.s.l.m.} & \\multirow{2}{*}{%.0f m.s.l.m.} & \\multirow{2}{*}{%.0f m} & \\multirow{2}{*}{%.0f m} & \\multirow{2}{*}{%.2f km} & \\multirow{2}{*}{%.2f kms} & \\multirow{2}{*}{%ld h %ld min} \\\\\n", round(minh), round(maxh), round(dsum_return(updh_sum)), round(dsum_return(downdh_sum)), km, kms, tot_time/60, tot_time%60);
    fprintf(sink, "        &&&&&& \\\\\n");
    fprintf(sink, "        \\hline\n");
    fprintf(sink, "    \\end{tabular}\\end{center}\n");

    fprintf(sink, "\n");
    fprintf(sink, "\\pagebreak\n");
    fprintf(sink, "\n");

    fprintf(sink, "    \\begin{center}\n");
    fprintf(sink, "        \\textsc{\\Huge %s}\n", name);
    fprintf(sink, "\n");
    fprintf(sink, "        \\vspace{2ex}\n");
    fprintf(sink, "\n");
    fprintf(sink, "\\textsc{\\large Profilo altimetrico}\n");
    fprintf(sink, "    \\end{center}\n");

    fprintf(sink, "\n");

    #define PLOT_MAX_X 25.0
    #define PLOT_MAX_Y 15.0

    #define CELL_SIZE 1.0
    fprintf(sink, "    \\begin{center}\\begin{tikzpicture}[x=%lfcm,y=%lfcm, step=%lfcm]\n", CELL_SIZE, CELL_SIZE, CELL_SIZE);

        fprintf(sink, "\\draw[very thin,color=black!20] (0.0,0.0) grid (%.1f,%.1f);\n", PLOT_MAX_X+0.5, PLOT_MAX_Y+0.5);

        fprintf(sink, "\\draw[->] (0,-0.5) -- (0,%.1f) node[above] {$h \\hphantom{i} [m]$};\n", PLOT_MAX_Y+0.2);
        fprintf(sink, "\\draw[->] (-0.5,0) -- (%.1f,0) node[right] {$s \\hphantom{i} [km]$};\n", PLOT_MAX_X+0.2);

        fprintf(sink, "\\filldraw[black] (0,0) rectangle (0.0,0.0) node[anchor=north east]{0};\n");

        for (size_t h = 1; h < (size_t) PLOT_MAX_Y + 1; h++) {
            fprintf(sink, "\\filldraw[black] (-0.05,%ld) rectangle (0.05, %ld) node[anchor=east]{%.0f};\n", h, h, ((double) h /PLOT_MAX_Y) * 3000.0);
        }

        for (size_t k = 1; k < (size_t) PLOT_MAX_X + 1; k++) {
            fprintf(sink, "\\filldraw[black] (%ld,-0.05) rectangle (%ld,0.05) node[anchor=north]{%.1f};\n", k, k, round(((double) k /PLOT_MAX_X) * km * 10.0)/10.0);
        }

        size_t index_step = max(path_len / GRAPH_POINTS_COUNT, 1);
        // printf("PATH OPTIMIZATION: %ld %ld\n", path_len, index_step);
        // fprintf(sink, "\\draw plot[smooth] coordinates{(0,0) ");
        // {
        //     double path_x = 0;
        //     double path_kms;
        //     for (size_t i = 0; i < path_len; i ++) {
        //         if (i > 0) {
        //             double dst = distance(&path[i-1], &path[i]);
        //             double dh = path[i].ele - path[i-1].ele;
        //             path_x += dst;

        //             path_kms += calculate_kms(dst, dh);
        //         }

        //         if (i % index_step == 0 || i == path_len - 1)
        //             fprintf(sink, "(%f, %f) ",
        //                 map(path_x, 0, km, 0, PLOT_MAX_X),
        //                 map(path_kms, 0, kms, 0, PLOT_MAX_Y));
        //     }
        // }
        // fprintf(sink, "};\n");

        fprintf(sink, "\\draw plot[smooth] coordinates{");
        double max_ele = -DBL_MAX, min_ele = DBL_MAX;
        double max_ele_x = 0.0, min_ele_x = 0.0;
        {
            double path_x = 0;
            for (size_t i = 0; i < path_len; i ++) {
                if (i > 0) path_x += distance(&path[i-1], &path[i]);
                if (path[i].ele < min_ele) {
                        min_ele = path[i].ele;
                        min_ele_x = path_x;
                }
                if (path[i].ele > max_ele) {
                        max_ele = path[i].ele;
                        max_ele_x = path_x;
                }

                if (i % index_step == 0 || i == path_len - 1)
                    fprintf(sink, "(%f, %f) ",
                        map(path_x, 0, km, 0, PLOT_MAX_X),
                        map(path[i].ele, 0, 3000.0, 0, PLOT_MAX_Y));
            }
        }
        fprintf(sink, "};\n");

        double triangle_y = map(max_ele, 0, 3000.0, 0, PLOT_MAX_Y) - 0.25;
        triangle_y = triangle_y < 0.0 ? 0.0 : triangle_y;
        fprintf(sink, "\\draw[black!50] (%f,%f) node[draw,isosceles triangle,isosceles triangle apex angle=60,draw,rotate=90, anchor=apex, scale=0.33, fill=black!50] {};\n", map(max_ele_x, 0, km, 0, PLOT_MAX_X), triangle_y);
        triangle_y = map(min_ele, 0, 3000.0, 0, PLOT_MAX_Y) + 0.25;
        triangle_y = triangle_y > PLOT_MAX_Y ? PLOT_MAX_Y : triangle_y;
        fprintf(sink, "\\draw[black!50] (%f,%f) node[draw,isosceles triangle,isosceles triangle apex angle=60,draw,rotate=270, anchor=apex, scale=0.33, fill=black!50] {};\n", map(min_ele_x, 0, km, 0, PLOT_MAX_X), triangle_y);
#if 0
        double ele_progress = map(max_ele_x, 0, km, 0, 1.0);
        if (ele_progress < 0.025) {
            fprintf(sink, "\\node[anchor=north west] at (%f,%f) {\\footnotesize %ld m};\n", map(max_ele_x, 0, km, 0, PLOT_MAX_X), map(max_ele, 0, 4000.0, 0, PLOT_MAX_Y) - 0.33, (uint64_t) (round(max_ele)));
        // } else if (ele_progress > 0.9) {
        //     fprintf(sink, "\\node[anchor=north east] at (%f,%f) {\\footnotesize %ld m};\n", map(max_ele_x, 0, km, 0, PLOT_MAX_X), map(max_ele, 0, 4000.0, 0, PLOT_MAX_Y) - 0.33, (uint64_t) (round(max_ele)));
        } else {
            fprintf(sink, "\\node[anchor=north] at (%f,%f) {\\footnotesize %ld m};\n", map(max_ele_x, 0, km, 0, PLOT_MAX_X), map(max_ele, 0, 4000.0, 0, PLOT_MAX_Y) - 0.33, (uint64_t) (round(max_ele)));
        }

        ele_progress = map(min_ele_x, 0, km, 0, 1.0);
        if (ele_progress < 0.025) {
            fprintf(sink, "\\node[anchor=south west] at (%f,%f) {\\footnotesize %ld m};\n", map(min_ele_x, 0, km, 0, PLOT_MAX_X), map(min_ele, 0, 4000.0, 0, PLOT_MAX_Y) + 0.33, (uint64_t) (round(min_ele)));
        // } else if (ele_progress > 0.9) {
        //     fprintf(sink, "\\node[anchor=south east] at (%f,%f) {\\footnotesize %ld m};\n", map(min_ele_x, 0, km, 0, PLOT_MAX_X), map(min_ele, 0, 4000.0, 0, PLOT_MAX_Y) + 0.33, (uint64_t) (round(min_ele)));
        } else {
            fprintf(sink, "\\node[anchor=south] at (%f,%f) {\\footnotesize %ld m};\n", map(min_ele_x, 0, km, 0, PLOT_MAX_X), map(min_ele, 0, 4000.0, 0, PLOT_MAX_Y) + 0.33, (uint64_t) (round(min_ele)));
        }
#endif

        double x = 0;
        for (size_t i = 0; i < waypoints_len; i++) {
            waypoint_name(i, wp_name);
            fprintf(sink, "\\filldraw[black] (%f,%f) circle (2pt) node[anchor=south west]{%.*s};\n", map(x, 0, km, 0, PLOT_MAX_X), map(waypoints[i].ele, 0, 3000.0, 0, PLOT_MAX_Y), 2, wp_name);
            x += segments[i].dst;
        }

    fprintf(sink, "    \\end{tikzpicture}\\end{center}\n");

    if (include_map)
        print_map(sink);
    
    fprintf(sink, "\\end{document}\n");
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
    printf("[INFO] Compiling LaTeX document... ");
    fflush(stdout);
    system(command);
    printf("done!\n");
}

void print_usage(const char* program) {
    printf("UTILIZZO: %s <path/to/file.gpx> [opzioni]\n", program);
    printf("\n");
    printf("Opzioni:    --pdf       Invoca automaticamente XeLaTeX per generare il file PDF.\n");
    printf("                        XeLaTeX deve essere installato perché ciò funzioni.\n");
    printf("            --map       Scarica le mappe ufficiali svizzere e le include nel\n");
    printf("                        documento LaTeX. CURL e ImageMagick devono essere\n");
    printf("                        installati.\n");
    printf("            -h,--help   Stampa il messaggio di aiuto, poi termina.\n");
}

int main(int argc, char* argv[]) {
    const char* program = *argv;
    char* file_path = NULL;
    int build_pdf = 0;
    int include_map = 0;

    while (--argc > 0) {
        argv++;

        if (strcmp(*argv+strlen(*argv)-4, ".gpx") == 0) {
            file_path = *argv;
        } else if (strcmp(*argv, "--pdf") == 0) {
            build_pdf = 1;
        } else if (strcmp(*argv, "--map") == 0) {
            include_map = 1;
        } else if (strcmp(*argv, "-h") == 0 || strcmp(*argv, "--help") == 0) {
            print_usage(program);
            return 0;
        } else {
            fprintf(stderr, "[ERRORE] Comando non riconosciuto: '%s'.\n", *argv);
            print_usage(program);
            return 1;
        }
    }

    if (file_path == NULL) {
        fprintf(stderr, "[ERRORE] Non è stato dato alcun file GPX.\n");
        print_usage(program);
        return 1;
    }
    snprintf(out_file_path, 128, "%.*s.tex", (int) strlen(file_path)-4, file_path);

    {
        double factor = 0;
        uint64_t hours = 0, mins = 0;
        printf("Vi prego d'inserire:\n");

        printf(" - Fattore di marcia (kms/h): ");
        scanf("%lf", &factor);
        if (factor > 0) FACTOR = factor;

        printf(" - Orario di partenza [hh:mm]: ");
        scanf("%zu:%zu", &hours, &mins);
        if ( (int64_t) hours >= 0 && (int64_t) mins >= 0) START_TIME = hours * 60 + mins;
        // printf("%f %ld:%ld\n\n\n", factor, hours, mins);
    }

    if (load_source(file_path) != 0) {
        return 1;
    }

    uint8_t* error_free_source = fix_source((uint8_t*) source+1);
    // printf("%ld\n", error_free_source-(uint8_t*)source);

    parse_gpx(error_free_source, file_path);

    // Output table
    // Output graph
    // Output latex doc
    FILE *out_file = fopen(out_file_path, "w");
    if (out_file == NULL) {
        out_file = stdout;
    }
    
    print_latex_document(out_file, include_map);

    // Create PDF
    if (out_file != stdout) {
        fclose(out_file);

        if (build_pdf) compile_latex();
    }

    return 0;
}