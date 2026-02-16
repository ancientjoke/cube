#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#ifdef _WIN32
    #include <windows.h>
    #include <conio.h>
    #define CLEAR_SCREEN() system("cls")
    #define SLEEP_MS(x) Sleep(x)
#else
    #include <unistd.h>
    #include <termios.h>
    #include <sys/select.h>
    #define CLEAR_SCREEN() printf("\033[2J\033[H")
    #define SLEEP_MS(x) usleep((x) * 1000)
#endif

#define PI 3.14159265358979323846
#define MAX_WIDTH 200
#define MAX_HEIGHT 100

typedef struct {
    float x, y, z;
} Vec3;

typedef struct {
    int width;
    int height;
    float scale;
    int quality;
    float rotation_speed;
    int color_mode;
    int texture_mode;
    int wireframe;
    int show_axes;
} Settings;

typedef struct {
    char buffer[MAX_HEIGHT][MAX_WIDTH];
    float zbuffer[MAX_HEIGHT][MAX_WIDTH];
} Screen;

const char* ascii_textures[] = {
    " .:-=+*#%@",
    " .,;!lI$&8#@",
    " .oO08@",
    " .:!*oe&#%@",
    "ABCDEFGHIJ",
    "0123456789",
    "░▒▓█",
    " `'^\",:;Il!i><~+_-?][}{1)(|\\/tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$"
};

const int num_textures = 8;

#ifdef _WIN32
void set_color(int color) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, color);
}
#else
void set_color(int color) {
    if (color == 0) {
        printf("\033[0m");
    } else if (color <= 8) {
        printf("\033[%dm", 30 + color - 1);
    } else {
        printf("\033[1;%dm", 30 + (color - 9));
    }
}
#endif

void init_screen(Screen* screen, int width, int height) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            screen->buffer[y][x] = ' ';
            screen->zbuffer[y][x] = -INFINITY;
        }
    }
}

void draw_screen(Screen* screen, Settings* settings) {
    CLEAR_SCREEN();
    for (int y = 0; y < settings->height; y++) {
        for (int x = 0; x < settings->width; x++) {
            putchar(screen->buffer[y][x]);
        }
        putchar('\n');
    }
    fflush(stdout);
}

Vec3 rotate_x(Vec3 v, float angle) {
    Vec3 result;
    result.x = v.x;
    result.y = v.y * cos(angle) - v.z * sin(angle);
    result.z = v.y * sin(angle) + v.z * cos(angle);
    return result;
}

Vec3 rotate_y(Vec3 v, float angle) {
    Vec3 result;
    result.x = v.x * cos(angle) + v.z * sin(angle);
    result.y = v.y;
    result.z = -v.x * sin(angle) + v.z * cos(angle);
    return result;
}

Vec3 rotate_z(Vec3 v, float angle) {
    Vec3 result;
    result.x = v.x * cos(angle) - v.y * sin(angle);
    result.y = v.x * sin(angle) + v.y * cos(angle);
    result.z = v.z;
    return result;
}

Vec3 project(Vec3 v, Settings* settings) {
    Vec3 result;
    float distance = 5.0f;
    float scale = settings->scale * (settings->width / 80.0f);
    
    result.x = (v.x * distance) / (v.z + distance) * scale + settings->width / 2.0f;
    result.y = (v.y * distance) / (v.z + distance) * scale + settings->height / 2.0f;
    result.z = v.z;
    
    return result;
}

void plot_point(Screen* screen, Settings* settings, int x, int y, float z, char c) {
    if (x >= 0 && x < settings->width && y >= 0 && y < settings->height) {
        if (z > screen->zbuffer[y][x]) {
            screen->buffer[y][x] = c;
            screen->zbuffer[y][x] = z;
        }
    }
}

void draw_line(Screen* screen, Settings* settings, Vec3 p1, Vec3 p2, char c) {
    int x0 = (int)p1.x;
    int y0 = (int)p1.y;
    int x1 = (int)p2.x;
    int y1 = (int)p2.y;
    
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    
    while (1) {
        float t = 0.0f;
        if (dx + dy > 0) {
            t = (float)(abs(x0 - (int)p1.x) + abs(y0 - (int)p1.y)) / (dx + dy);
        }
        float z = p1.z + (p2.z - p1.z) * t;
        
        plot_point(screen, settings, x0, y0, z, c);
        
        if (x0 == x1 && y0 == y1) break;
        
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

float get_brightness(Vec3 normal, Vec3 light_dir) {
    float dot = normal.x * light_dir.x + normal.y * light_dir.y + normal.z * light_dir.z;
    return fmax(0.0f, dot);
}

char get_ascii_char(float brightness, int texture_mode) {
    const char* texture = ascii_textures[texture_mode];
    int len = strlen(texture);
    int index = (int)(brightness * (len - 1));
    if (index < 0) index = 0;
    if (index >= len) index = len - 1;
    return texture[index];
}

void fill_triangle(Screen* screen, Settings* settings, Vec3 p1, Vec3 p2, Vec3 p3, 
                   Vec3 n, Vec3 light_dir, int texture_mode) {
    if (p1.y > p2.y) { Vec3 tmp = p1; p1 = p2; p2 = tmp; }
    if (p1.y > p3.y) { Vec3 tmp = p1; p1 = p3; p3 = tmp; }
    if (p2.y > p3.y) { Vec3 tmp = p2; p2 = p3; p3 = tmp; }
    
    float brightness = get_brightness(n, light_dir);
    char c = get_ascii_char(brightness, texture_mode);
    
    int y_start = (int)fmax(0, p1.y);
    int y_end = (int)fmin(settings->height - 1, p3.y);
    
    for (int y = y_start; y <= y_end; y++) {
        float alpha = (p3.y - p1.y) != 0 ? (y - p1.y) / (p3.y - p1.y) : 0;
        Vec3 A, B;
        
        if (y < p2.y) {
            float beta = (p2.y - p1.y) != 0 ? (y - p1.y) / (p2.y - p1.y) : 0;
            A.x = p1.x + (p3.x - p1.x) * alpha;
            A.z = p1.z + (p3.z - p1.z) * alpha;
            B.x = p1.x + (p2.x - p1.x) * beta;
            B.z = p1.z + (p2.z - p1.z) * beta;
        } else {
            float beta = (p3.y - p2.y) != 0 ? (y - p2.y) / (p3.y - p2.y) : 0;
            A.x = p1.x + (p3.x - p1.x) * alpha;
            A.z = p1.z + (p3.z - p1.z) * alpha;
            B.x = p2.x + (p3.x - p2.x) * beta;
            B.z = p2.z + (p3.z - p2.z) * beta;
        }
        
        if (A.x > B.x) {
            float tmpx = A.x; A.x = B.x; B.x = tmpx;
            float tmpz = A.z; A.z = B.z; B.z = tmpz;
        }
        
        int x_start = (int)fmax(0, A.x);
        int x_end = (int)fmin(settings->width - 1, B.x);
        
        for (int x = x_start; x <= x_end; x++) {
            float t = (B.x - A.x) != 0 ? (x - A.x) / (B.x - A.x) : 0;
            float z = A.z + (B.z - A.z) * t;
            plot_point(screen, settings, x, y, z, c);
        }
    }
}

Vec3 compute_normal(Vec3 p1, Vec3 p2, Vec3 p3) {
    Vec3 u, v, n;
    u.x = p2.x - p1.x; u.y = p2.y - p1.y; u.z = p2.z - p1.z;
    v.x = p3.x - p1.x; v.y = p3.y - p1.y; v.z = p3.z - p1.z;
    
    n.x = u.y * v.z - u.z * v.y;
    n.y = u.z * v.x - u.x * v.z;
    n.z = u.x * v.y - u.y * v.x;
    
    float len = sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    if (len > 0) {
        n.x /= len; n.y /= len; n.z /= len;
    }
    
    return n;
}

void draw_cube(Screen* screen, Settings* settings, float angle_x, float angle_y, float angle_z) {
    float size = 1.0f;
    
    Vec3 vertices[8] = {
        {-size, -size, -size}, {size, -size, -size}, {size, size, -size}, {-size, size, -size},
        {-size, -size, size},  {size, -size, size},  {size, size, size},  {-size, size, size}
    };
    
    for (int i = 0; i < 8; i++) {
        vertices[i] = rotate_x(vertices[i], angle_x);
        vertices[i] = rotate_y(vertices[i], angle_y);
        vertices[i] = rotate_z(vertices[i], angle_z);
    }
    
    Vec3 light_dir = {0.5f, 0.5f, 1.0f};
    float len = sqrt(light_dir.x * light_dir.x + light_dir.y * light_dir.y + light_dir.z * light_dir.z);
    light_dir.x /= len; light_dir.y /= len; light_dir.z /= len;
    
    int faces[6][4] = {
        {0, 1, 2, 3}, {4, 7, 6, 5}, {0, 4, 5, 1},
        {2, 6, 7, 3}, {0, 3, 7, 4}, {1, 5, 6, 2}
    };
    
    typedef struct {
        int face_idx;
        float avg_z;
    } FaceSort;
    
    FaceSort sorted_faces[6];
    for (int i = 0; i < 6; i++) {
        sorted_faces[i].face_idx = i;
        sorted_faces[i].avg_z = 0;
        for (int j = 0; j < 4; j++) {
            sorted_faces[i].avg_z += vertices[faces[i][j]].z;
        }
        sorted_faces[i].avg_z /= 4.0f;
    }
    
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < 5 - i; j++) {
            if (sorted_faces[j].avg_z > sorted_faces[j + 1].avg_z) {
                FaceSort tmp = sorted_faces[j];
                sorted_faces[j] = sorted_faces[j + 1];
                sorted_faces[j + 1] = tmp;
            }
        }
    }
    
    for (int f = 0; f < 6; f++) {
        int face_idx = sorted_faces[f].face_idx;
        int* face = faces[face_idx];
        
        Vec3 v0 = vertices[face[0]];
        Vec3 v1 = vertices[face[1]];
        Vec3 v2 = vertices[face[2]];
        Vec3 v3 = vertices[face[3]];
        
        Vec3 normal = compute_normal(v0, v1, v2);
        
        Vec3 view_dir = {0, 0, 1};
        float dot = normal.x * view_dir.x + normal.y * view_dir.y + normal.z * view_dir.z;
        
        if (dot < 0) continue;
        
        Vec3 p0 = project(v0, settings);
        Vec3 p1 = project(v1, settings);
        Vec3 p2 = project(v2, settings);
        Vec3 p3 = project(v3, settings);
        
        if (!settings->wireframe) {
            fill_triangle(screen, settings, p0, p1, p2, normal, light_dir, settings->texture_mode);
            fill_triangle(screen, settings, p0, p2, p3, normal, light_dir, settings->texture_mode);
        } else {
            char edge_char = '#';
            draw_line(screen, settings, p0, p1, edge_char);
            draw_line(screen, settings, p1, p2, edge_char);
            draw_line(screen, settings, p2, p3, edge_char);
            draw_line(screen, settings, p3, p0, edge_char);
        }
    }
}

void draw_axes(Screen* screen, Settings* settings, float angle_x, float angle_y, float angle_z) {
    float axis_length = 1.5f;
    
    Vec3 origin = {0, 0, 0};
    Vec3 x_axis = {axis_length, 0, 0};
    Vec3 y_axis = {0, axis_length, 0};
    Vec3 z_axis = {0, 0, axis_length};
    
    origin = rotate_x(origin, angle_x);
    origin = rotate_y(origin, angle_y);
    origin = rotate_z(origin, angle_z);
    
    x_axis = rotate_x(x_axis, angle_x);
    x_axis = rotate_y(x_axis, angle_y);
    x_axis = rotate_z(x_axis, angle_z);
    
    y_axis = rotate_x(y_axis, angle_x);
    y_axis = rotate_y(y_axis, angle_y);
    y_axis = rotate_z(y_axis, angle_z);
    
    z_axis = rotate_x(z_axis, angle_x);
    z_axis = rotate_y(z_axis, angle_y);
    z_axis = rotate_z(z_axis, angle_z);
    
    Vec3 p_origin = project(origin, settings);
    Vec3 p_x = project(x_axis, settings);
    Vec3 p_y = project(y_axis, settings);
    Vec3 p_z = project(z_axis, settings);
    
    draw_line(screen, settings, p_origin, p_x, 'X');
    draw_line(screen, settings, p_origin, p_y, 'Y');
    draw_line(screen, settings, p_origin, p_z, 'Z');
}

void print_help() {
    printf("\n=== ASCII Rotating Cube - Controls ===\n\n");
    printf("Quality Controls:\n");
    printf("  + / -     : Increase/decrease quality (detail level)\n");
    printf("  w / h     : Increase/decrease width\n");
    printf("  e / d     : Increase/decrease height\n");
    printf("\nSpeed Controls:\n");
    printf("  SPACE     : Pause/resume rotation\n");
    printf("  < / >     : Decrease/increase rotation speed\n");
    printf("  , / .     : Fine speed adjustment\n");
    printf("\nVisual Controls:\n");
    printf("  c         : Cycle through color modes\n");
    printf("  t         : Cycle through texture modes\n");
    printf("  f         : Toggle wireframe mode\n");
    printf("  a         : Toggle axes display\n");
    printf("  s         : Increase scale\n");
    printf("  x         : Decrease scale\n");
    printf("\nOther:\n");
    printf("  r         : Reset to defaults\n");
    printf("  i         : Show this help\n");
    printf("  q         : Quit\n");
    printf("\nPress any key to continue...\n");
    getchar();
}

void print_status(Settings* settings) {
    printf("\n[Q:%d W:%d H:%d Speed:%.2f Scale:%.1f Color:%d Texture:%d Wire:%s Axes:%s]\n",
           settings->quality, settings->width, settings->height, settings->rotation_speed,
           settings->scale, settings->color_mode, settings->texture_mode,
           settings->wireframe ? "ON" : "OFF", settings->show_axes ? "ON" : "OFF");
    printf("Press 'i' for help, 'q' to quit\n");
}

#ifndef _WIN32
int kbhit(void) {
    struct timeval tv = {0L, 0L};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(0, &fds);
    return select(1, &fds, NULL, NULL, &tv) > 0;
}

int getch(void) {
    struct termios oldt, newt;
    int ch;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    ch = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return ch;
}
#endif

void apply_quality(Settings* settings) {
    switch(settings->quality) {
        case 1:
            settings->width = 40;
            settings->height = 20;
            settings->scale = 8.0f;
            break;
        case 2:
            settings->width = 60;
            settings->height = 30;
            settings->scale = 12.0f;
            break;
        case 3:
            settings->width = 80;
            settings->height = 40;
            settings->scale = 16.0f;
            break;
        case 4:
            settings->width = 100;
            settings->height = 50;
            settings->scale = 20.0f;
            break;
        case 5:
            settings->width = 120;
            settings->height = 60;
            settings->scale = 24.0f;
            break;
        default:
            settings->width = 80;
            settings->height = 40;
            settings->scale = 16.0f;
            break;
    }
    
    if (settings->width > MAX_WIDTH) settings->width = MAX_WIDTH;
    if (settings->height > MAX_HEIGHT) settings->height = MAX_HEIGHT;
}

void reset_settings(Settings* settings) {
    settings->quality = 3;
    settings->rotation_speed = 0.02f;
    settings->color_mode = 0;
    settings->texture_mode = 0;
    settings->wireframe = 0;
    settings->show_axes = 0;
    apply_quality(settings);
}

int main(int argc, char* argv[]) {
    Settings settings;
    reset_settings(&settings);
    
    Screen screen;
    
    float angle_x = 0.0f;
    float angle_y = 0.0f;
    float angle_z = 0.0f;
    
    int paused = 0;
    int show_status = 1;
    
    printf("=== ASCII Rotating Cube ===\n");
    printf("\nStarting with default settings...\n");
    printf("Press 'i' during animation for controls help\n");
    printf("Press any key to start...\n");
    getchar();
    
    CLEAR_SCREEN();
    
    while (1) {
        init_screen(&screen, settings.width, settings.height);
        
        draw_cube(&screen, &settings, angle_x, angle_y, angle_z);
        
        if (settings.show_axes) {
            draw_axes(&screen, &settings, angle_x, angle_y, angle_z);
        }
        
        if (settings.color_mode > 0) {
            set_color(settings.color_mode);
        }
        
        draw_screen(&screen, &settings);
        
        if (settings.color_mode > 0) {
            set_color(0);
        }
        
        if (show_status) {
            print_status(&settings);
        }
        
        if (!paused) {
            angle_x += settings.rotation_speed;
            angle_y += settings.rotation_speed * 1.3f;
            angle_z += settings.rotation_speed * 0.7f;
        }
        
        SLEEP_MS(16);
        
        if (kbhit()) {
            int ch = getch();
            
            switch(ch) {
                case 'q':
                case 'Q':
                    CLEAR_SCREEN();
                    printf("Thanks for using ASCII Rotating Cube!\n");
                    return 0;
                
                case ' ':
                    paused = !paused;
                    break;
                
                case '+':
                case '=':
                    if (settings.quality < 5) {
                        settings.quality++;
                        apply_quality(&settings);
                    }
                    break;
                
                case '-':
                case '_':
                    if (settings.quality > 1) {
                        settings.quality--;
                        apply_quality(&settings);
                    }
                    break;
                
                case 'w':
                case 'W':
                    if (settings.width < MAX_WIDTH - 10) settings.width += 10;
                    break;
                
                case 'h':
                case 'H':
                    if (settings.width > 20) settings.width -= 10;
                    break;
                
                case 'e':
                case 'E':
                    if (settings.height < MAX_HEIGHT - 5) settings.height += 5;
                    break;
                
                case 'd':
                case 'D':
                    if (settings.height > 10) settings.height -= 5;
                    break;
                
                case '>':
                case '.':
                    settings.rotation_speed += 0.01f;
                    if (settings.rotation_speed > 0.2f) settings.rotation_speed = 0.2f;
                    break;
                
                case '<':
                case ',':
                    settings.rotation_speed -= 0.01f;
                    if (settings.rotation_speed < 0.001f) settings.rotation_speed = 0.001f;
                    break;
                
                case 'c':
                case 'C':
                    settings.color_mode = (settings.color_mode + 1) % 16;
                    break;
                
                case 't':
                case 'T':
                    settings.texture_mode = (settings.texture_mode + 1) % num_textures;
                    break;
                
                case 'f':
                case 'F':
                    settings.wireframe = !settings.wireframe;
                    break;
                
                case 'a':
                case 'A':
                    settings.show_axes = !settings.show_axes;
                    break;
                
                case 's':
                case 'S':
                    settings.scale += 2.0f;
                    if (settings.scale > 50.0f) settings.scale = 50.0f;
                    break;
                
                case 'x':
                case 'X':
                    settings.scale -= 2.0f;
                    if (settings.scale < 4.0f) settings.scale = 4.0f;
                    break;
                
                case 'r':
                case 'R':
                    reset_settings(&settings);
                    angle_x = angle_y = angle_z = 0.0f;
                    break;
                
                case 'i':
                case 'I':
                    CLEAR_SCREEN();
                    print_help();
                    break;
                
                default:
                    break;
            }
        }
    }
    
    return 0;
}