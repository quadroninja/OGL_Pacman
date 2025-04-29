#include <windows.h>
#include <gl/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define EPS 0.000001 //при работе с float нельзя проверять значения на равенство напрямую, нужно сравнить их разность с некоторым бесконечно малым числом

#define FRAME_COUNT 2 //количество кадров анимации пакмана
#define TIME_TO_MOVE 350.0
#define TIME_CHANGE_FRAME 200.0
#define BONUS_DURATION 20000.0 //длительность бонусов в миллисек.
#define INTERFACE_POINT 0.03 //размер буквы в инфо-табло сверху экрана

#define CHAR_SIZE 1.0/16.0 //текстовые символы берутся из атласа 16x16, который будет располагаться в I четверти координатной плоскости, поэтому 1.0/16.0 - размер одного символа в атласе
#define LEVELS 3 //сколько уровней


LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
void EnableOpenGL(HWND hwnd, HDC*, HGLRC*);
void DisableOpenGL(HWND, HDC, HGLRC);


enum states{MOVING, STAYING};
enum foodTypes{EMPTY, FOOD, BREAKER_BONUS, SPEED_BONUS};
enum directions{UP, RIGHT, DOWN, LEFT};
int dir_koefs[4][2] = {{-1, 0},
                       {0, 1},
                       {1, 0},
                       {0, -1}};
typedef struct Player{
    float offset_x, offset_y; //переменные, используемые для анимации движения, лежат в промежутке [-1;1], 1 - отклонение на одну клетку
    int i, j; //строка и столбец клетки, в которой находится пакман
    float timeToMove; //время, за которое пакман переходит из клетки в клетку (в миллисек.) по умолчанию: 1000.0
    float timeChangeFrame; //время, за которое сменится кадр (в миллисек)
    int currentFrame; //текущий кадр анимации, лежит в полуинтервале [0;FRAME_COUNT) (НЕВКЛЮЧИТЕЛЬНО!!)
    char state; //состояние игрока: двигается или стоит, по умолчанию: STAYING (стоит)
    char dir; //направление пакмана на игровом поле, по умолчанию: UP (наверх)
    char canBreakWalls; //способность игрока ломать стены, 0 - не может, 1 - может
} TPlayer;
typedef struct Rect{float left, right, top, bottom} TRect;

int level = 0;
char* levels[] = {"map1.txt",
                  "map2.txt",
                  "map3.txt"};
enum gameStates{GAME_ACTIVE, GAME_INTERMEDIATE, GAME_PAUSE, GAME_WIN};
int gameState = GAME_INTERMEDIATE;


TPlayer player;
long long curTime, prevTime;
float movementTimer = 0, animTimer = 0, allTimer = 0;
int n, m;
char** matrix;
char** textureTypes;
char** foodMap;

int score = 0; //счет (не совпадает с кол-вом съеденных горошин, потому что бонусы дают по 5 очков)
int neededToWin = 0; //число горошин вначале
int remaining; //число горошин на любой момент игры

float speedBonusTimer = 0;
float breakerBonusTimer = 0;

char didRelease = 1; //при переходе между уровнями и выигрыше обнуляется, чтобы не пропустить экраны
int fullscreen = 0;

float vertex[] = {0,0,0, 1,0,0, 1,1,0, 0,1,0};
float texCoord[] = {0,1, 1,1, 1,0, 0,0}; //текстурные координаты для всех объектов
float textTexCoord[] = {0,1, 1,1, 1,0, 0,0}; //текстурные координаты для символов


unsigned int texturesGranite[2], textureGrass, texturesPacman[2],
             textureFood, textureSpeed, textureBreaker,
             textAtlas, textureTrophy, winScreen,
             intermediateScreen, pauseScreen;
unsigned int *pixel_data;


long long time_in_ms(void);
void set_matrix_row(int row);
char is_in_bounds(int i, int j);
void draw_symbol(char sym);
void load_texture(char* name, unsigned int *texture);
void load_textures(void);
void clear_player(void);
void clear_data(void);
void init_map(char* map_name);
void init_game(void);
void draw_texture(unsigned int* texture);
void draw_terrain(int i, int j);
void draw_pacman(void);
void draw_food(int i, int j);
void update_game(float delta); //delta - разница времени между двумя обновлениями в миллисек.
void draw_menu(void);
void draw_pause(void);
void intermediate_screen(void);
void win_screen(void);
void draw_green_mist(void);
void draw_red_mist(void);



WINDOWPLACEMENT wpc;


int WINAPI WinMain(HINSTANCE hInstance,
                   HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine,
                   int nCmdShow)
{
    WNDCLASSEX wcex;
    HWND hwnd;
    HDC hDC;
    HGLRC hRC;
    MSG msg;
    BOOL bQuit = FALSE;
    float theta = 0.0f;

    /* register window class */
    wcex.cbSize = sizeof(WNDCLASSEX);
    wcex.style = CS_OWNDC;
    wcex.lpfnWndProc = WindowProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wcex.lpszMenuName = NULL;
    wcex.lpszClassName = "GLSample";
    wcex.hIconSm = LoadIcon(NULL, IDI_APPLICATION);;


    if (!RegisterClassEx(&wcex))
        return 0;

    /* create main window */
    hwnd = CreateWindowEx(0,
                          "GLSample",
                          "OpenGL Sample",
                          WS_OVERLAPPEDWINDOW | WS_MINIMIZEBOX | WS_SYSMENU | WS_VISIBLE,
                          CW_USEDEFAULT,
                          CW_USEDEFAULT,
                          1440,
                          900,
                          NULL,
                          NULL,
                          hInstance,
                          NULL);
    GetWindowLong(hwnd, GWL_STYLE);
    ShowWindow(hwnd, nCmdShow);


    /* enable OpenGL for the window */
    EnableOpenGL(hwnd, &hDC, &hRC);

    //инициализация тут
    srand(time(NULL));
    load_textures();

    prevTime = time_in_ms();
    curTime = prevTime;

    restart_point:

    init_map(levels[level]);
    init_game();
    /* program main loop */
    while (!bQuit)
    {
        /* check for messages */
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            /* handle or dispatch messages */
            if (msg.message == WM_QUIT)
            {
                bQuit = TRUE;
            }
            else
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
        else
        {
            /* OpenGL animation code goes here */
            prevTime = curTime;
            curTime = time_in_ms();

            if (gameState == GAME_ACTIVE)
            {
                update_game((float)(curTime - prevTime));
            }
            else if (gameState == GAME_INTERMEDIATE)
            {
                draw_intermediate();
                SwapBuffers(hDC);
                continue;
            }
            else if (gameState == GAME_WIN)
            {
                win_screen();
                SwapBuffers(hDC);
                continue;
            }


            if (remaining == 0)
            {
                printf("level %d passed\n", level);
                clear_data();
                level++;
                if (level > LEVELS - 1)
                {
                    gameState = GAME_WIN;
                    continue;
                }
                else
                {
                    gameState = GAME_INTERMEDIATE;
                }
                goto restart_point;
            }

            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);



            glLoadIdentity();

            glScalef(2.0f / m, 2.0f / n, 1);
            glTranslatef(-m*0.5f, -n*0.5f, 0);

            for (int i = 0; i < n; i++)
            {
                for (int j = 0; j < m; j++)
                {
                    glPushMatrix();
                        glTranslatef(j, (n-i-1) * 0.90, 0);
                        draw_terrain(i, j);
                        draw_food(i, j);
                    glPopMatrix();
                }
            }
            glPushMatrix();
                glTranslatef(player.j + player.offset_x, (n-player.i-1 + player.offset_y) * 0.90, 0);
                draw_pacman();
            glPopMatrix();

            draw_menu();



            if (gameState == GAME_PAUSE)
            {
                draw_pause();
            }



            SwapBuffers(hDC);
            Sleep(1);
        }
    }
    printf("n=%d m=%d\n", n, m);
    for (int i = 0; i < n; i++)
    {
        for (int j = 0; j < m; j++)
        {
            char toPrint = matrix[i][j] == '#' ? 176 : 32;
            printf("%c%c", toPrint, toPrint);
        }
            printf("\n");
    }


    clear_data();

    /* shutdown OpenGL */
    DisableOpenGL(hwnd, hDC, hRC);

    /* destroy the window explicitly */
    DestroyWindow(hwnd);

    return msg.wParam;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        case WM_CREATE:
            if (!fullscreen)
            {
                GetWindowPlacement(hwnd, &wpc);
                SetWindowLong(hwnd, GWL_STYLE, WS_POPUP);
                SetWindowLong(hwnd, GWL_STYLE, WS_EX_TOPMOST);
                ShowWindow(hwnd, SW_SHOWMAXIMIZED);
            }
            else
            {
                SetWindowLong(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
                SetWindowLong(hwnd, GWL_EXSTYLE, 0L);
                SetWindowPlacement(hwnd, &wpc);
                ShowWindow(hwnd, SW_SHOWDEFAULT);
                fullscreen = 0;
            }
        break;
        case WM_CLOSE:
            PostQuitMessage(0);
        break;

        case WM_DESTROY:
            return 0;

        case WM_KEYUP:
            didRelease = 1;
        break;

        case WM_KEYDOWN:
        {
            if (didRelease == 1)
            {
                if (gameState == GAME_INTERMEDIATE)
                {
                    gameState = GAME_ACTIVE;
                }
                if (gameState == GAME_WIN)
                {
                    PostQuitMessage(0);
                }
                didRelease = 0;
            }
            switch (wParam)
            {
                case 'P':
                    if (gameState == GAME_PAUSE)
                        gameState = GAME_ACTIVE;
                    else if (gameState == GAME_ACTIVE)
                        gameState = GAME_PAUSE;
                break;
                case VK_ESCAPE:
                    PostQuitMessage(0);
                break;
            }
        }
        break;

        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    return 0;
}

void EnableOpenGL(HWND hwnd, HDC* hDC, HGLRC* hRC)
{
    PIXELFORMATDESCRIPTOR pfd;

    int iFormat;

    /* get the device context (DC) */
    *hDC = GetDC(hwnd);

    /* set the pixel format for the DC */
    ZeroMemory(&pfd, sizeof(pfd));

    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW |
                  PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.cDepthBits = 16;
    pfd.iLayerType = PFD_MAIN_PLANE;

    iFormat = ChoosePixelFormat(*hDC, &pfd);

    SetPixelFormat(*hDC, iFormat, &pfd);

    /* create and enable the render context (RC) */
    *hRC = wglCreateContext(*hDC);

    wglMakeCurrent(*hDC, *hRC);
}

void DisableOpenGL (HWND hwnd, HDC hDC, HGLRC hRC)
{
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(hRC);
    ReleaseDC(hwnd, hDC);
}



long long time_in_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (((long long)tv.tv_sec) * 1000) + (tv.tv_usec / 1000);
}

void set_matrix_row(int row)
{
    matrix[row] = (char*)calloc(m, sizeof(char));
}

char is_in_bounds(int i, int j)
{
    return (i >= 0) && (i < n) && (j >= 0) && (j < m);
}


void draw_symbol(char sym)
{
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, textAtlas);
    //glPushMatrix();

        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);

        glVertexPointer(3, GL_FLOAT, 0, vertex);
        glTexCoordPointer(2, GL_FLOAT, 0, textTexCoord);

        int y = sym / 16;
        int x = sym % 16;
        TRect rct;
        rct.left = x*CHAR_SIZE;
        rct.right = rct.left + CHAR_SIZE;
        rct.top = y*CHAR_SIZE;
        rct.bottom = rct.top + CHAR_SIZE;
        textTexCoord[0] = textTexCoord[6] = rct.left;
        textTexCoord[2] = textTexCoord[4] = rct.right;
        textTexCoord[1] = textTexCoord[3] = rct.bottom;
        textTexCoord[5] = textTexCoord[7] = rct.top;

        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

        glDisableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    //glPopMatrix();
    glBindTexture(GL_TEXTURE_2D, 0);
}


void load_texture(char* name, unsigned int *texture)
{
    int width, height, cnt;
    pixel_data = stbi_load((const char*)name, &width, &height, &cnt, 0);
    if (!pixel_data)
    {
        printf("Cannot load image: \"%s\", code: %s\n", name, stbi_failure_reason());
        return;
    }
    glGenTextures(1, texture);
    glBindTexture(GL_TEXTURE_2D, *texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, cnt == 4 ? GL_RGBA : GL_RGB, width, height,
                                    0, cnt == 4 ? GL_RGBA : GL_RGB, GL_UNSIGNED_BYTE, pixel_data);
    glBindTexture(GL_TEXTURE_2D, 0);
    printf("%s loaded!\n", name);
}
void load_textures()
{
    load_texture("dirt.jpg", &textureGrass);
    load_texture("box1.jpg", &texturesGranite[0]);
    load_texture("box2.jpg", &texturesGranite[1]);
    load_texture("pacman1.png", &texturesPacman[0]);
    load_texture("pacman2.png", &texturesPacman[1]);
    load_texture("trophy.png", &textureTrophy);
    load_texture("winscreen.jpg", &winScreen);
    load_texture("pause.png", &pauseScreen);
    load_texture("intermediate.png", &intermediateScreen);

    load_texture("food.png", &textureFood);
    load_texture("speedbonus.png", &textureSpeed);
    load_texture("breakerbonus.png", &textureBreaker);


    load_texture("Verdana_B_alpha.png", &textAtlas);
    stbi_image_free(pixel_data);
}


void clear_player()
{
    player.dir = UP;
    player.canBreakWalls = 0;
    player.currentFrame = 0;
    player.i = -1;
    player.j = -1;
    player.offset_x = 0;
    player.offset_y = 0;
    player.state = STAYING;
    player.timeChangeFrame = TIME_CHANGE_FRAME;
    player.timeToMove = TIME_TO_MOVE;
}

void clear_data()
{
    for (int i = 0; i < n; i++)
    {
        free(matrix[i]);
        free(foodMap[i]);
        free(textureTypes[i]);
    }
    free(matrix);
    free(foodMap);
    free(textureTypes);
    neededToWin = 0;
    remaining = 0;

    speedBonusTimer = 0;
    breakerBonusTimer = 0;

    clear_player();

}

void init_map(char* map_name)
{
    FILE* ptr;
    ptr = fopen((const char*)map_name, "r");
    if (ptr == NULL)
    {
        printf("Can't open %s!\n", map_name);
        return 0;
    }
    int i = 0, j = 0;
    char ch;
    char buffer[10000];
    do {
        ch = fgetc(ptr);
        buffer[i] = ch;
        i++;
    } while (ch != EOF && ch != '\n');

    m = i - 1;

    matrix = (char**)malloc(2 * sizeof(char*));
    set_matrix_row(0);
    set_matrix_row(1);

    for (i = 0; i < m; i++)
        matrix[0][i] = buffer[i];
    i = 1; j = 0;
    int currentChunk = 2;
    do {
        ch = fgetc(ptr);
        if (ch == '\n' || j >= m)
        {
            i++;
            while (i >= currentChunk - 1)
            {
                currentChunk *= 2;
                matrix = (char**)realloc(matrix, currentChunk * sizeof(char*));
            }
            set_matrix_row(i);
            j = 0;
            continue;
        }
        else
        {
            matrix[i][j] = ch;
            j++;
        }
    } while (ch != EOF);
    n = i;
    fclose(ptr);
}


void init_game(void)
{

    textureTypes = (char**)malloc(n * sizeof(char*));
    for (int i = 0; i < n; i++) textureTypes[i] = (char*)malloc(m * sizeof(char));

    foodMap = (char**)malloc(n * sizeof(char*));
    for (int i = 0; i < n; i++) foodMap[i] = (char*)calloc(m, sizeof(char));


    player.dir = UP;
    player.state = STAYING;
    player.timeToMove = TIME_TO_MOVE;
    player.offset_x = 0;
    player.offset_y = 0;
    player.currentFrame = 0;
    player.timeChangeFrame = TIME_CHANGE_FRAME;
    player.canBreakWalls = 0;
    player.i = -1; player.j = -1; //неинициализированные значения, недопустимые на игровом поле. Как только пакман найдется, это станет понятно по этим значениям

    for (int i = 0; i < n; i++)
        for (int j = 0; j < m; j++)
        {
            textureTypes[i][j] = rand()%2;
            if (matrix[i][j] == 'P') //проверка на наличие пакмана на карте. Если их несколько, то учитывается лишь первая просмотренная, остальные превращаются в пустые клетки
            {
                if (player.i == -1)
                {
                    player.i = i;
                    player.j = j;
                }
                else
                    matrix[i][j] = '0';
            }
            if (matrix[i][j] == 'F')
            {
                foodMap[i][j] = FOOD;
                neededToWin++;
            }
            else if (matrix[i][j] == 'B')
                foodMap[i][j] = BREAKER_BONUS;
            else if (matrix[i][j] == 'S')
                foodMap[i][j] = SPEED_BONUS;
        }
        remaining = neededToWin;
}





void draw_texture(unsigned int* texture)
{
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, *texture);
    glColor3f(1, 1, 1);
    glPushMatrix();
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);

        glVertexPointer(3, GL_FLOAT, 0, vertex);
        glTexCoordPointer(2, GL_FLOAT, 0, texCoord);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

        glDisableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glBindTexture(GL_TEXTURE_2D, 0);
    glPopMatrix();
}


void draw_terrain(int i, int j)
{

    glEnable(GL_TEXTURE_2D);
    if (matrix[i][j] == '#')
        glBindTexture(GL_TEXTURE_2D, texturesGranite[textureTypes[i][j]]);
    else
        glBindTexture(GL_TEXTURE_2D, textureGrass);

    glColor3f(1, 1, 1);
    glPushMatrix();
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);

        glVertexPointer(3, GL_FLOAT, 0, vertex);
        glTexCoordPointer(2, GL_FLOAT, 0, texCoord);
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

        glDisableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glBindTexture(GL_TEXTURE_2D, 0);
    glPopMatrix();
}


void draw_pacman()
{
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);


    glBindTexture(GL_TEXTURE_2D, texturesPacman[player.currentFrame]);

    glColor3f(1, 1, 1);
    glPushMatrix();
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);

        glVertexPointer(3, GL_FLOAT, 0, vertex);
        glTexCoordPointer(2, GL_FLOAT, 0, texCoord);


        glPushMatrix();
            if (player.dir == LEFT) //если использовать общий алгоритм поворота, то пакман будет перевернут, что не очень красиво. Поэтому мы сначала отражаем его по оси y, только потом поворачиваем
            {
                glRotatef(180, 0, 1, 0);
                glTranslatef(-1.0f, 0.0f, 0.0f);
                glTranslatef(0.5, 0.5, 0.0);
                glRotatef(-90, 0, 0, 1);
                glTranslatef(-0.5, -0.5, 0.0);
            }
            else
            {
                glTranslatef(0.5, 0.5, 0.0);
                glRotatef(-90 * player.dir, 0, 0, 1);
                glTranslatef(-0.5, -0.5, 0.0);
            }

            glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        glPopMatrix();

        glDisableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glBindTexture(GL_TEXTURE_2D, 0);
    glPopMatrix();

}


void draw_food(int i, int j)
{
    if (foodMap[i][j] == EMPTY)
        return;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);

    if (foodMap[i][j] == FOOD)
        glBindTexture(GL_TEXTURE_2D, textureFood);
    if (foodMap[i][j] == BREAKER_BONUS)
        glBindTexture(GL_TEXTURE_2D, textureBreaker);
    if (foodMap[i][j] == SPEED_BONUS)
        glBindTexture(GL_TEXTURE_2D, textureSpeed);

    glColor3f(1, 1, 1);
    glPushMatrix();
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);

        glVertexPointer(3, GL_FLOAT, 0, vertex);
        glTexCoordPointer(2, GL_FLOAT, 0, texCoord);

        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

        glDisableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glBindTexture(GL_TEXTURE_2D, 0);
    glPopMatrix();

}



void update_game(float delta) //delta - разница времени между двумя обновлениями в миллисек.
{
    animTimer += delta;
    allTimer += delta;
    speedBonusTimer = max(0, speedBonusTimer - delta);
    breakerBonusTimer = max(0, breakerBonusTimer - delta);
    if (speedBonusTimer < EPS)
        player.timeToMove = TIME_TO_MOVE;
    if (breakerBonusTimer < EPS)
        player.canBreakWalls = 0;

    if (animTimer > player.timeChangeFrame) //смена кадра анимации пакмана
    {
        animTimer = 0;
        player.currentFrame = (player.currentFrame + 1) % FRAME_COUNT;
    }

    if (movementTimer > player.timeToMove) //момент, когда игрок прошел путь из клетки в клетку
    {
        player.state = STAYING;
        player.offset_x = 0;
        player.offset_y = 0;
        movementTimer = 0;
        player.i += dir_koefs[player.dir][0];
        player.j += dir_koefs[player.dir][1];

        if (player.j >= m)
        {
            player.j = -1;
            player.dir = RIGHT;
            player.state = MOVING;
            return;
        }
        else if (player.j < 0)
        {
            player.j = m;
            player.dir = LEFT;
            player.state = MOVING;
            return;
        }
        else if (player.i >= n)
        {
            player.i = -1;
            player.dir = DOWN;
            player.state = MOVING;
            return;
        }
        else if (player.i < 0)
        {
            player.i = n;
            player.dir = UP;
            player.state = MOVING;
            return;
        }

        if (foodMap[player.i][player.j] == FOOD)
        {
            score++;
            remaining--;
        }
        if (foodMap[player.i][player.j] == SPEED_BONUS)
        {
            score += 5;
            player.timeToMove = TIME_TO_MOVE / 2;
            speedBonusTimer = BONUS_DURATION;
        }
        if (foodMap[player.i][player.j] == BREAKER_BONUS)
        {
            score += 5;
            player.canBreakWalls = 1;
            breakerBonusTimer = BONUS_DURATION;
        }
        foodMap[player.i][player.j] = EMPTY;
    }
    if (player.state == MOVING) //игрок еще не прошел путь из клетки в клетку
    {
        player.offset_x = max(-1, min(player.offset_x + dir_koefs[player.dir][1] * 1.0 / player.timeToMove * delta, 1));
        player.offset_y = max(-1, min(player.offset_y - dir_koefs[player.dir][0] * 1.0 / player.timeToMove * delta, 1));
        movementTimer += delta;
        return;
    }

    if (GetKeyState('A') < 0)
    {
        player.dir = LEFT;
        movementTimer = 0;
        player.state = MOVING;
    }
    else if (GetKeyState('D') < 0)
    {
        player.dir = RIGHT;
        movementTimer = 0;
        player.state = MOVING;
    }
    else if (GetKeyState('W') < 0)
    {
        player.dir = UP;
        movementTimer = 0;
        player.state = MOVING;
    }
    else if (GetKeyState('S') < 0)
    {
        player.dir = DOWN;
        movementTimer = 0;
        player.state = MOVING;
    }

    int nextTileI = player.i + dir_koefs[player.dir][0];
    if (nextTileI < 0) nextTileI = n-1;
    if (nextTileI >= n) nextTileI = 0;
    int nextTileJ = player.j + dir_koefs[player.dir][1];
    if (nextTileJ < 0) nextTileJ = m-1;
    if (nextTileJ >= m) nextTileJ = 0;


    if (is_in_bounds(nextTileI, nextTileJ) &&
        matrix[nextTileI][nextTileJ] == '#')
    {
        if (player.canBreakWalls)
        {
            matrix[nextTileI][nextTileJ] = '0';
            score++;
        }
        else
            player.state = STAYING;
    }


}



void draw_menu()
{
    if (GetKeyState(VK_SPACE) < 0)
        return;
    glPushMatrix();
        glLoadIdentity();
        glTranslatef(-1.0f, 0.81f, 0.0f);
        glScalef(2.0f, 0.2f, 0.0f);

        glColor3f(0.25, 0.34, 0.3);


        glVertexPointer(3, GL_FLOAT, 0, &vertex);
        glEnableClientState(GL_VERTEX_ARRAY);

        glDrawArrays(GL_QUADS, 0, 4);

        glColor3f(1, 0, 0);

        int shownScore = min(999, score);
        int shownRemaining = min(999, remaining);
        float shownSpeedBonusTimer = min(99999.9, speedBonusTimer);
        float shownBreakerBonusTimer = min(99999.9, breakerBonusTimer);


        char info[(int)(1.0 / INTERFACE_POINT) + 3];
        info[0] = -1;//тут кубок
        info[1] = -1;
        info[2] = '0' + shownScore / 100 % 10;
        info[3] = '0' + shownScore / 10 % 10;
        info[4] = '0' + shownScore % 10;
        info[5] = -1;
        info[6] = -1; //тут горошина
        info[7] = -1;
        info[8] = '0' + shownRemaining / 100 % 10;
        info[9] = '0' + shownRemaining / 10 % 10;
        info[10] = '0' + shownRemaining % 10;
        info[11] = -1;
        info[12] = -1; //тут бонус скорости
        info[13] = -1;
        info[14] = '0' + (int)shownSpeedBonusTimer / 10000 % 10;
        info[15] = '0' + (int)shownSpeedBonusTimer / 1000 % 10;
        info[16] = '.';
        info[17] = '0' + (int)shownSpeedBonusTimer / 100 % 10;
        info[18] = -1;
        info[19] = -1; //тут бонус стенолом
        info[20] = -1;
        info[21] = -1;
        info[22] = '0' + (int)shownBreakerBonusTimer / 10000 % 10;
        info[23] = '0' + (int)shownBreakerBonusTimer / 1000 % 10;
        info[24] = '.';
        info[25] = '0' + (int)shownBreakerBonusTimer / 100 % 10;
        info[26] = -1;
        for (int i = 27; i < (int)(1.0/INTERFACE_POINT) + 3; i++) info[i] = -1;
        glPushMatrix();
            glTranslatef(0.0f, 0.05f, 0.0f);
            glScalef(INTERFACE_POINT, 0.90f, 1.0f);

            glTranslatef(0.0f, 0.0f, 0.0f);
            glScalef(2.0f, 1.0f, 1.0f);
            draw_texture(&textureTrophy);
            glScalef(0.5f, 1.0f, 1.0f);

            glTranslatef(6.0f, 0.0f, 0.0f);
            glScalef(2.0f, 1.0f, 1.0f);
            draw_texture(&textureFood);
            glScalef(0.5f, 1.0f, 1.0f);

            glTranslatef(6.0f, 0.0f, 0.0f);
            glScalef(2.0f, 1.0f, 1.0f);
            draw_texture(&textureSpeed);
            glScalef(0.5f, 1.0f, 1.0f);

            glTranslatef(8.0f, 0.0f, 0.0f);
            glScalef(2.0f, 1.0f, 1.0f);
            draw_texture(&textureBreaker);
            glScalef(0.5f, 1.0f, 1.0f);

        glPopMatrix();
        glPushMatrix();
            glColor3f(0.0f, 0.7f, 0.1f);
            glTranslatef(0.0f, 0.05f, 0.0f);
            glScalef(INTERFACE_POINT, 0.90f, 1.0f);
            for (int i = 0; i < 26; i++)
            {
                if (info[i] != -1)
                    draw_symbol(info[i]);
                glTranslatef(1.0f, 0.0f, 0.0f);
            }
        glPopMatrix();
    glPopMatrix();
}


void win_screen()
{
    glPushMatrix();
        glLoadIdentity();
        glTranslatef(-1.0f, -1.0f, 0.0);
        glScalef(2.0f, 2.0f, 1.0f);

        draw_texture(&winScreen);
        glTranslatef(0.3, 0.088, 0.0);
        glScalef(0.0675, 0.050, 1.0);
        int counter = 0, counter1 = 0, scoreCopy = score, allTimerCopy = allTimer;
        char digits[1000]; //цифры получаем остатком от деления на 10, поэтому обратный порядок цифр, нужно будет перевернуть
        char digitsTime[1000];
        while(scoreCopy != 0)
        {
            digits[counter] = '0' + scoreCopy % 10;
            counter++;
            scoreCopy /= 10;
        }
        allTimerCopy /= 100;
        while(allTimerCopy != 0)
        {
            digitsTime[counter1] = '0' + allTimerCopy % 10;
            counter1++;
            allTimerCopy /= 10;
        }

        counter--;
        counter1--;
        glPushMatrix();
            glColor3f(0, 0, 0);
            for (counter; counter > -1; counter--)
            {
                draw_symbol(digits[counter]);
                glTranslatef(1.0f, 0.0f, 0.0f);
            }
        glPopMatrix();
        glPushMatrix();
            glTranslatef(0.0f, 1.3f, 0.0f);
            for (counter1; counter1 > 0; counter1--)
            {
                draw_symbol(digitsTime[counter1]);
                glTranslatef(1.0f, 0.0f, 0.0f);
            }
            draw_symbol('.');
            glTranslatef(1.0f, 0.0f, 0.0f);
            draw_symbol(digitsTime[counter1]);
            glTranslatef(1.0f, 0.0f, 0.0f);
            draw_symbol('s');
        glPopMatrix();

    glPopMatrix();

}
void draw_pause(void)
{
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);

    glPushMatrix();
        glLoadIdentity();
        glTranslatef(-1.0f, -1.0f, 0.0);
        glScalef(2.0f, 2.0f, 1.0f);

        draw_texture(&pauseScreen);

    glPopMatrix();
}


void draw_intermediate(void)
{
    glPushMatrix();
        glLoadIdentity();
        glTranslatef(-1.0f, -1.0f, 0.0);
        glScalef(2.0f, 2.0f, 1.0f);

        draw_texture(&intermediateScreen);
        glTranslatef(0.468, 0.765, 0.0);
        glScalef(0.039 * 2, 0.048 * 2, 1.0);
        int counter = 0, levelCopy = level + 1;
        char digits[1000]; //цифры получаем остатком от деления на 10, поэтому обратный порядок цифр, нужно будет перевернуть

        while(levelCopy != 0)
        {
            digits[counter] = '0' + levelCopy % 10;
            counter++;
            levelCopy /= 10;
        }

        counter--;
        glPushMatrix();
            glColor3f(1, 1, 1);
            for (counter; counter > -1; counter--)
            {
                draw_symbol(digits[counter]);
                glTranslatef(1.0f, 0.0f, 0.0f);
            }
        glPopMatrix();
    glPopMatrix();
}
