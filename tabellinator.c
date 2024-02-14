#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <locale.h>
#include <time.h>

#include "xml.c"

#ifndef M_PI
    #define M_PI 3.14159265358979323846
#endif

#define TILE_WIDTH 18000
#define TILE_HEIGHT 12000

typedef struct {
    double e; // lv95
    double n;
    double ele; // m
} Point;

typedef struct {
    double dst; // km
    double dh; // m
    double kms; // kms
    uint64_t t; // minutes
    uint64_t pause; // minutes
} PathSegmentData;

double FACTOR = 5.0; // kms/h
double PAUSE_FACTOR = 15.0/60.0; // min/min
uint64_t START_TIME =  0 * 60 + 0; // min
uint64_t PAUSA_PRANZO = 0 * 60 + 0;
int64_t PAUSA_PRANZO_IDX = 0;

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

#define PATH_CAPACITY 128 * 1000
Point path[PATH_CAPACITY] = {0};
size_t path_len = 0;

#define PATH_SEGMENTS_CAP WAYPOINTS_CAPACITY
PathSegmentData segments[PATH_SEGMENTS_CAP] = {0};
size_t segments_len = 0;

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

void waypoint_name(const size_t idx, char name[2]) {
    char letter = ((idx%26) + 'A');
    char number = (idx / 26) + '0';
    if (number == '0') {
        number = ' ';
    }
    assert(number <= '9' && "Too many waypoints");
    name[0] = letter;
    name[1] = number;
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
            double time = 60.0 * (ps->kms / FACTOR);

            ps->t = (uint64_t) round(time);
            
            if (wp_idx == (size_t) PAUSA_PRANZO_IDX) {
                (ps+1)->pause = PAUSA_PRANZO;
            } else {
                (ps+1)->pause = (uint64_t) round5(time * PAUSE_FACTOR);
            }

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
    // printf("Waypoint count:     %ld\n", waypoints_len);
    {
        size_t idx = 0;
        uint64_t hours = 0, mins = 0;
        printf(" - Waypoint in cui fare pausa pranzo [%d-%ld]: ", 0, waypoints_len-1);
        scanf("%zu", &idx);
        // printf("idx: %ld", idx);
        if (idx < waypoints_len && (int64_t) idx >= 0) {
            PAUSA_PRANZO_IDX = idx;
            printf(" - Durata pausa pranzo [hh:mm]: ");
            scanf("%zu:%zu", &hours, &mins);
            if ((int64_t) hours >= 0 && (int64_t) mins >= 0)
                PAUSA_PRANZO = hours * 60 + mins;
        }
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
    uint64_t y = (1302000 - N)/TILE_HEIGHT;
    uint64_t x = (E - 2480000)/TILE_WIDTH;
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
    fprintf(sink, "        \\textsc{\\Huge %s}\n", name);
    fprintf(sink, "\n");
    fprintf(sink, "        \\vspace{2ex}\n");
    fprintf(sink, "\n");
    fprintf(sink, "\\textsc{\\large Cartina}\n");
    fprintf(sink, "    \\end{center}\n");

    fprintf(sink, "\n");

    double cell_size = 0.8;
    fprintf(sink, "\\begin{center}\\begin{tikzpicture}[x=0.8cm,y=0.8cm, step=0.8cm] \n");
    // fprintf(sink, "\\draw[very thin,color=black!10] (0.0,0.0) grid (%.1lf,-%.1lf);\n", 30.0+0.5, 20.0+0.5);

    {
        uint64_t map_ids[] = {0, 0, 0, 0};
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

        uint64_t width = (maxE - minE) * 7 / 5;
        uint64_t height = (maxN - minN) * 7 / 5;

        minE = minE - width / 7;
        maxE = maxE + width / 7;
        minN = minN - height / 7;
        maxN = maxN + height / 7;
        printf("MIN %ld %ld\n", minE, minN);
        printf("MAX %ld %ld\n", maxE, maxN);

        double scalex = (double) TILE_WIDTH / (double) width;
        double scaley = (double) TILE_HEIGHT / (double) height;

        double scale = (scalex < scaley ? scalex : scaley);
        if(scale < 1.0) {
            scale = 1.0;
        }

        // printf("Scale: %f\n", scale);

        map_ids[0] = lv95_to_tileid(minE, minN);
        map_ids[1] = lv95_to_tileid(maxE, minN);
        map_ids[2] = lv95_to_tileid(minE, maxN);
        map_ids[3] = lv95_to_tileid(maxE, maxN);
        // printf("%ld %ld %ld %ld\n", minE, minN, maxE, maxN);
        // printf("%ld %ld %ld %ld\n", map_ids[0], map_ids[1], map_ids[2], map_ids[3]);

        for (size_t i = 0; i < 4; i++) {
            for (size_t j = i + 1; j < 4; j++) {
                if (map_ids[i] == map_ids[j]) {
                    map_ids[j] = 0;
                }
            }
        }

        size_t count_maps = 0;
        for (size_t i = 0; i < 4; i++) {
            count_maps += map_ids[i] != 0;
        }

        size_t t = time(NULL);

        size_t checked_ids_size = 0;
        uint64_t checked_ids[32] = {0};
        size_t frame_id = 0;
        for (uint64_t e = minE; e <= maxE; e += (TILE_WIDTH / 2)) {
            for (uint64_t n = maxN; n >= minN; n -= (TILE_HEIGHT / 2)) {
                uint64_t id = lv95_to_tileid(e, n);

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
                snprintf(jpg_file, 15, "%ld-2.jpg", id);
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
                    system(get_url_cmd);
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
                    system(convert_cmd);
                }

                // cropping

                const uint64_t full_image_size_x = 3500;
                const uint64_t full_image_size_y = 2400;

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
                // printf("%ld %ld %ld %ld\n", min_contained_E, min_contained_N, max_contained_E, max_contained_N);

                // get the size of the cropped image in pixels
                double cropped_map_width = (double) (max_contained_E - min_contained_E) / (double) TILE_WIDTH;
                double cropped_map_height = (double) (max_contained_N - min_contained_N) /  (double) TILE_HEIGHT;
                uint64_t cropped_image_width =  (uint64_t) ((double) full_image_size_x * cropped_map_width / scale);
                uint64_t cropped_image_height = (uint64_t) ((double) full_image_size_y * cropped_map_height / scale);

                // get the offset from the center of the image
                double coord_offset_x = (double) (min_contained_E - mapMinE) / (double) TILE_WIDTH;
                double coord_offset_y = (double) (mapMaxN - max_contained_N) / (double) TILE_HEIGHT;
                // printf("COORD OFFSETS: %f %f\n", coord_offset_x, coord_offset_y);5000
                
                int64_t pixel_offset_x = (int64_t) (coord_offset_x * (double)full_image_size_x);
                int64_t pixel_offset_y = (int64_t)  (coord_offset_y * (double)full_image_size_y);
                // printf("PX OFFSETS: %ld %ld\n", pixel_offset_x, pixel_offset_y);

                //construct command
                char call_cmd[256] = {0};
                snprintf(call_cmd, 256, "magick %s -crop %ldx%ld%+ld%+ld %s", jpg_file, cropped_image_width, cropped_image_height, pixel_offset_x, pixel_offset_y, map_file);
                // printf("[CROP] %s\n", call_cmd);
                system(call_cmd);
                // call command

                // assert images are only one
                // put image in latex

                // find coordinates in page
                uint64_t center_N = (max_contained_N + min_contained_N) / 2;
                uint64_t center_E = (max_contained_E + min_contained_E) / 2;
                double aspect_ratio = (double) width / (double) height;
                // double ax = ((min_contained_E - (double) minE) / (double) height) * 20.0;
                // double ay = (((double) maxN - max_contained_N) / (double) height) * 20.0;
                // double bx = ((max_contained_E - (double) minE) / (double) height) * 20.0;
                // double by = (((double) maxN - min_contained_N) / (double) height) * 20.0;
                double x = map(center_E, minE, minE + height, 0.0, 20.0);
                double y = map(center_N, maxN, minN, 0.0, 20.0);
                // find dimensions
                double w = (((double) (max_contained_E - min_contained_E) / (double) height)) * cell_size * 20.0;
                double h = (((double) (max_contained_N - min_contained_N) / (double) height)) * cell_size * 20.0;
                
                fprintf(sink, "\\node[inner sep=0pt] (russel) at (%lf, -%lf) {\\includegraphics[width=%lfcm, height=%lfcm]{%s}};\n", x, y, w, h, map_file);
                // fprintf(sink, "\\filldraw[black] (%lf, -%lf) circle (2pt) node[anchor=south west]{%s};\n", x, y, map_file);
                
                frame_id += 1;

                // draw path
                // draw waypoints

                // printf("%s\n", url);
            }
        }

        fprintf(sink, "\\draw[red] plot[smooth] coordinates{");

        size_t index_step = path_len / 1000;
        for (size_t i = 0; i < path_len; i ++) {
            if (i % index_step == 0 || i == path_len - 1)
                fprintf(sink, "(%f, %f) ",
                    map(path[i].e, minE, height+minE, 0, 20.0),
                    map(path[i].n, maxN, minN, 0, -20.0));
        }
        fprintf(sink, "};\n");

        char wp_name[2] = {0};
        for (size_t i = 0; i < waypoints_len; i++) {
            waypoint_name(i, wp_name);
            fprintf(sink, "\\filldraw[red] (%f,%f) circle (1pt) node[anchor=south west]{\\footnotesize %.*s};\n",
                map(waypoints[i].e, minE, height+minE, 0, 20.0),
                map(waypoints[i].n, maxN, minN, 0, -20.0),
                2, wp_name);
        }

        // fprintf(sink, "\\filldraw[red] (%lf,%lf) circle (2pt) node[anchor=south west]{%s};\n", 0.0, 0.0, "A");
        // fprintf(sink, "\\filldraw[red] (%lf,%lf) circle (2pt) node[anchor=south west]{%s};\n", 20.0, 0.0, "B");
        // fprintf(sink, "\\filldraw[red] (%lf,%lf) circle (2pt) node[anchor=south west]{%s};\n", 0.0, -20.0, "C");
        // fprintf(sink, "\\filldraw[red] (%lf,%lf) circle (2pt) node[anchor=south west]{%s};\n", 20.0, -20.0, "D");
        fprintf(sink, "\\end{tikzpicture}\\end{center}\n");
    }
}

void print_latex_document(FILE* sink) {
    #define DOC_MARGIN 1.0
    setlocale(LC_NUMERIC, "");

    fprintf(sink, "\\documentclass[a4paper,10pt,landscape]{article}\n");
    fprintf(sink, "\\usepackage{multirow}\n");
    fprintf(sink, "\\usepackage[margin=%.0fcm]{geometry}\n", DOC_MARGIN);
    fprintf(sink, "\\usepackage{microtype}\n");
    fprintf(sink, "\\usepackage{longtable}\n");
    fprintf(sink, "\\usepackage{graphicx}\n");
    fprintf(sink, "\\usepackage{tikz}\n");
    fprintf(sink, "\\usepgflibrary{plotmarks}\n");
    fprintf(sink, "\\usetikzlibrary{positioning}\n");

    fprintf(sink, "\n");
    fprintf(sink, "\\begin{document}\n");
    fprintf(sink, "    \\pagenumbering{gobble}\n");
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
    fprintf(sink, "    \\begin{center}\\begin{tabular}{|c|c||c|c|}\n");
    fprintf(sink, "        \\hline\n");
    fprintf(sink, "        \\multirow{2}{*}{Fattore di pausa:} & \\multirow{2}{*}{$%0.2f \\hphantom{a} \\frac{min}{h} $} & \\multirow{2}{*}{Fattore di marcia:} & \\multirow{2}{*}{$%0.1f \\hphantom{a} \\frac{kms}{h}$}\\\\\n", PAUSE_FACTOR * 60.0, FACTOR);
    fprintf(sink, "        &&&\\\\\n");
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

    double minx = 4000, maxx = 0, updh = 0, downdh = 0;
    for (size_t i = 0; i < path_len; i++) {
        Point p = path[i];
        if (p.ele < minx) minx = p.ele;
        if (p.ele > maxx) maxx = p.ele;
        if (i > 0) {
            Point p_ = path[i-1];
            double dh = p.ele - p_.ele;
            if (dh > 0) updh += dh; else downdh += dh;
        }
    }
    uint64_t tot_time = (uint64_t) round(60.0 * kms / FACTOR);
    fprintf(sink, "        \\multirow{2}{*}{%.0f m.s.l.m.} & \\multirow{2}{*}{%.0f m.s.l.m.} & \\multirow{2}{*}{%.0f m} & \\multirow{2}{*}{%.0f m} & \\multirow{2}{*}{%.2f km} & \\multirow{2}{*}{%.2f kms} & \\multirow{2}{*}{%ld h %ld min} \\\\\n", round(minx), round(maxx), round(updh), round(-downdh), km, kms, tot_time/60, tot_time%60);
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

    #define PLOT_MAX_X 30.0
    #define PLOT_MAX_Y 20.0

    fprintf(sink, "    \\begin{center}\\begin{tikzpicture}[x=0.8cm,y=0.8cm, step=0.8cm]\n");

        fprintf(sink, "\\draw[very thin,color=black!10] (0.0,0.0) grid (%.1f,%.1f);\n", PLOT_MAX_X+0.5, PLOT_MAX_Y+0.5);

        fprintf(sink, "\\draw[->] (0,-0.5) -- (0,%.1f) node[above] {$h \\hphantom{i} [m]$};\n", PLOT_MAX_Y+0.2);
        fprintf(sink, "\\draw[->] (-0.5,0) -- (%.1f,0) node[right] {$s \\hphantom{i} [km]$};\n", PLOT_MAX_X+0.2);

        fprintf(sink, "\\filldraw[black] (0,0) rectangle (0.0,0.0) node[anchor=north east]{0};\n");

        for (size_t h = 1; h < (size_t) PLOT_MAX_Y + 1; h++) {
            fprintf(sink, "\\filldraw[black] (-0.05,%ld) rectangle (0.05, %ld) node[anchor=east]{%.0f};\n", h, h, ((double) h /PLOT_MAX_Y) * 4000.0);
        }

        for (size_t k = 1; k < (size_t) PLOT_MAX_X + 1; k++) {
            fprintf(sink, "\\filldraw[black] (%ld,-0.05) rectangle (%ld,0.05) node[anchor=north]{%.1f};\n", k, k, round(((double) k /PLOT_MAX_X) * km * 10.0)/10.0);
        }

        fprintf(sink, "\\draw plot[smooth] coordinates{");

        double path_x = 0;
        size_t index_step = path_len / 1000;
        // printf("PATH OPTIMIZATION: %ld %ld\n", path_len, index_step);
        for (size_t i = 0; i < path_len; i ++) {
            if (i > 0) path_x += distance(&path[i-1], &path[i]);

            if (i % index_step == 0 || i == path_len - 1)
                fprintf(sink, "(%f, %f) ",
                    map(path_x, 0, km, 0, PLOT_MAX_X),
                    map(path[i].ele, 0, 4000.0, 0, PLOT_MAX_Y));
        }
        fprintf(sink, "};\n");

        double x = 0;
        for (size_t i = 0; i < waypoints_len; i++) {
            waypoint_name(i, wp_name);
            fprintf(sink, "\\filldraw[black] (%f,%f) circle (2pt) node[anchor=south west]{%.*s};\n", map(x, 0, km, 0, PLOT_MAX_X), map(waypoints[i].ele, 0, 4000.0, 0, PLOT_MAX_Y), 2, wp_name);
            x += segments[i].dst;
        }

    fprintf(sink, "    \\end{tikzpicture}\\end{center}\n");

#if 0
    print_map(sink);
#endif
    
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
    system(command);
}

void print_usage(const char* program) {
    printf("UTILIZZO: %s <path/to/file.gpx> [--pdf]\n", program);
    printf("\n");
    printf("Opzioni:    --pdf       Invoca automaticamente xelatex per generare il file PDF.\n");
    printf("                        XeLaTeX deve essere installato perché ciò funzioni.\n");
    printf("            -h,--help   Stampa il messaggio di aiuto, poi termina.\n");
}

int main(int argc, char* argv[]) {
    const char* program = *argv;
    char* file_path = NULL;
    int build_pdf = 0;

    while (--argc > 0) {
        argv++;

        if (strcmp(*argv+strlen(*argv)-4, ".gpx") == 0) {
            file_path = *argv;
        } else if (strcmp(*argv, "--pdf") == 0) {
            build_pdf = 1;
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
    // printf("%ld\n", error_free_source-(uint8_t*)source);

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

        if (build_pdf) compile_latex();
    }

    return 0;
}