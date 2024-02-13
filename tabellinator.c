#include "xml.c"

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

#include "map.c"

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
