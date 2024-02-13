#include <stdint.h>
#include <stdio.h>

#define TILE_WIDTH 18000
#define TILE_HEIGHT 12000

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

    fprintf(sink, "\\begin{tikzpicture}\n");

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
            N = 0;
            E = 0;
        }

        uint64_t width = (maxE - minE) * 7 / 5;
        uint64_t height = (maxN - minN) * 7 / 5;

        minE = minE - width / 7;
        maxE = maxE + width / 7;
        minN = minN - height / 7;
        maxN = maxN + height / 7;

        double scalex = (double) TILE_WIDTH / (double) width;
        double scaley = (double) TILE_HEIGHT / (double) height;

        double scale = (scalex < scaley ? scalex : scaley);
        if(scale < 1.0) {
            scale = 1.0;
        }

        printf("Scale: %f\n", scale);

        map_ids[0] = lv95_to_tileid(minE, minN);
        map_ids[1] = lv95_to_tileid(maxE, minN);
        map_ids[2] = lv95_to_tileid(minE, maxN);
        map_ids[3] = lv95_to_tileid(maxE, maxN);
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

        for (size_t i = 0; i < 4; i++) {
            if (map_ids[i] == 0) continue;

            char tiff_file[16] = {0};
            char jpg_file[16] = {0};
            char map_file[24] = {0};
            snprintf(tiff_file, 15, "%ld.tif", map_ids[i]);
            snprintf(jpg_file, 15, "%ld-0.jpg", map_ids[i]);
            snprintf(map_file, 24, "map-%ld-%05ld.jpg", map_ids[i], time(NULL)%100000);

            uint64_t year = get_year(map_ids[0]);

            if (!file_exists(tiff_file)) {
                char get_url_cmd[256] = {0};
                snprintf(get_url_cmd, 255, "curl https://data.geo.admin.ch/ch.swisstopo.pixelkarte-farbe-pk25.noscale/swiss-map-raster25_%ld_%ld/swiss-map-raster25_%ld_%ld_krel_1.25_2056.tif > %s"
#ifdef _WIN32
                    " > nul"
#else
                    " 2> /dev/null"
#endif
                , year, map_ids[0], year, map_ids[0], tiff_file);
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
                , tiff_file, (int) strlen(tiff_file)-4,tiff_file);
                system(convert_cmd);
            }

            // cropping

            const uint64_t full_image_size_x = 14000;
            const uint64_t full_image_size_y = 9600;

            // get the coordinates contained in the map
            int64_t mapMinE = 0, mapMinN = 0;
            int64_t mapMaxE = 0, mapMaxN = 0;

            tileid_coord(map_ids[i], (uint64_t*) &mapMinE, (uint64_t*) &mapMinN);
            mapMaxE = mapMinE + TILE_WIDTH;
            mapMaxN = mapMinN + TILE_HEIGHT;
            printf("MAP: %ld %ld %ld %ld\n", mapMinE, mapMinN, mapMaxE, mapMaxN);
            printf("MINIMAP: %ld %ld %ld %ld\n", minE, minN, maxE, maxN);

            int64_t min_contained_E = 0, min_contained_N = 0;
            int64_t max_contained_E = 0, max_contained_N = 0;

            min_contained_E = mapMinE > (int64_t) minE ? (int64_t) mapMinE : (int64_t) minE;
            max_contained_E = mapMaxE < (int64_t) maxE ? (int64_t) mapMaxE : (int64_t) maxE;
            min_contained_N = mapMinN > (int64_t) minN ? (int64_t) mapMinN : (int64_t) minN;
            max_contained_N = mapMaxN < (int64_t) maxN ? (int64_t) mapMaxN : (int64_t) maxN;
            printf("%ld %ld %ld %ld\n", min_contained_E, min_contained_N, max_contained_E, max_contained_N);

            // get the size of the cropped image in pixels
            uint64_t cropped_image_width =  (uint64_t) ((double) full_image_size_x / scale);
            uint64_t cropped_image_height = (uint64_t) ((double) full_image_size_y / scale);

            // get the offset from the center of the image
            double coord_offset_x = (double) (min_contained_E - mapMinE) * 2.0 / (double) TILE_WIDTH;
            double coord_offset_y = (double) (mapMaxN - max_contained_N) / (double) TILE_HEIGHT;
            printf("COORD OFFSETS: %f %f\n", coord_offset_x, coord_offset_y);
            
            int64_t pixel_offset_x = (int64_t) (coord_offset_x * (double)full_image_size_x );
            int64_t pixel_offset_y =(int64_t)  (coord_offset_y * (double)full_image_size_y);
            printf("PX OFFSETS: %ld %ld\n", pixel_offset_x, pixel_offset_y);

            //construct command
            char call_cmd[256] = {0};
            snprintf(call_cmd, 256, "magick %s -crop %ldx%ld%+ld%+ld %s", jpg_file, cropped_image_width, cropped_image_height, pixel_offset_x, pixel_offset_y, map_file);
            // printf("yooo\n");
            system(call_cmd);
            // call command

            // assert images are only one
            // put image in latex
            // draw path
            // draw waypoints

            fprintf(sink, "\\node[inner sep=0pt] (russell) at (0,0){\\includegraphics[height=0.9\\textheight]{%s}};\n", map_file);

            // printf("%s\n", url);
        }

        fprintf(sink, "\\end{tikzpicture}\n");
    }
}