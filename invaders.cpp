// Include SDK headers
#include <windows.h>
#include <gl/gl.h>

// OpenGL extensions
// this header might be not found so include it into project directory
#include <gl/wglext.h>

extern "C" double sqrt(double);

// Compiler warning level must be set to W1
// OpenGL mode set
void SetupPixelFormat(HDC hdc) {
    static PIXELFORMATDESCRIPTOR pfd = {
        sizeof(PIXELFORMATDESCRIPTOR), 1,
        PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
        PFD_TYPE_RGBA, 32, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        16, 0, 0, PFD_MAIN_PLANE, 0, 0, 0, 0
    };
    int index = ChoosePixelFormat(hdc, &pfd);
    SetPixelFormat(hdc, index, &pfd);
}

// we need extensions to disable vsync
bool WGLExtensionSupported(const char *extension_name) {
    PFNWGLGETEXTENSIONSSTRINGEXTPROC _wglGetExtensionsStringEXT = NULL;
    _wglGetExtensionsStringEXT = (PFNWGLGETEXTENSIONSSTRINGEXTPROC) wglGetProcAddress("wglGetExtensionsStringEXT");
    if (strstr(_wglGetExtensionsStringEXT(), extension_name) == NULL) return false;
    return true;
}

// High precision timer in nanoseconds
unsigned __int64 fast_tick() {
  static unsigned __int64 u, f = (unsigned __int64 )-1;
  QueryPerformanceCounter((_LARGE_INTEGER*)&u);
  if (f == (unsigned __int64)-1) {
    QueryPerformanceFrequency((_LARGE_INTEGER*)&f);
    f /= 1000; f /= 1000;
  }
  return u / f;
}

// High precision timer in microseconds
int time1000() { return fast_tick() / 1000; }

// Simple custom pseudo-random number generator
struct _rnd {
   unsigned int a,b,c,d;
   _rnd() {  seed(time1000()); } //seed(0);
   void seed(int x) {
      x = x * 10000000 + 10;
      a = x; b = x; c = x; d = x;
      for (int i = 0; i < 10000; i++) next();
   }
   unsigned int next() {
     unsigned int i;
     a ++; b += a; c += b; d += c; i = a + b + c + d;
     return i;
   }
   double next(double max) { // return random number between zero and "max"
      return (double) next() / ((double)0xffffffff / max);
   }
} rnd;

// WINDOW AND UI variables

bool quit = false; // application state
int cursor_x, cursor_y; // store current cursor position globally

// The string that appears in the application's title bar.
static wchar_t *szTitle = L"-=Space*invaders=-";
int app_width = 800, app_height = 600; // app size is fixed
HDC global_hdc;  // A global handle to the device context

// GAME LOGIC

//used by invader
struct color {
  double red, green, blue, alpha;
};

// Store invaders as structures in the array

struct invader {
  int
    status,    // 0-empty, 1-newly teleported, 2-normal, 3-hit, 4-reached the ground
               // (we only use 0-1 for now)
    frame_id;  // animation frame reference number for f/x
  double       // double allows smoother animation
    position,  // displacement from left edge of the screen to the center of the invader
    altitude,  // distance from the ground
    hull_size; // diameter of the invader
  color war_paint;
};

const int maximum_attack = 10; // how many maximum invaders on screen
int attack = 0; // current attack power (number of invaders visible now)
int scores = 0; // user's score
double min_hull_size = 10; // enemy ship size range
double max_hull_size = 100;
double attack_speed = 1.5; // global multiplier for the speed of enemies, AKA difficulty level

// this is fixed preallocated array, because normally
// we do not want any memory allocations/deallocations during the game loop.
invader enemies[maximum_attack];

// the enemy ships on the screen can collide with each other, but for better looks
// we will not allow them to collide when they appear at the top of the screen,
// they still can collide later.

bool radar_detect_collision(int the_one) {
  // collision math, extremely simple distance check
  for (int i = 0; i < maximum_attack; i++) {
    if (i == the_one) continue; // do not compare with itself
    if (enemies[i].status == 0) continue; // skip empty slots
    invader &a = enemies[the_one], &b = enemies[i];
    double x = (a.position - b.position), y = a.altitude - b.altitude;
    double distance = sqrt(x * x + y * y);
    if (distance < (a.hull_size + b.hull_size) / 2) // simply put, hull_size is a diameter
       return true;
  }
  return false;
}

// create (teleport) the new ship at the top of the screep
// check for collision with existing ships

void teleport_invader() {
  for (int i = 0; i < maximum_attack; i++) if (enemies[i].status == 0) {
  // ship is a shortcut to access the new invader structure and make code more readable
     invader &ship = enemies[i];

  // random hull size is limited by constants min/max
     ship.hull_size = rnd.next(max_hull_size - min_hull_size) + min_hull_size;

  // position to fit inside the screen
     int width = app_width;
     ship.position = rnd.next((double) (width - ship.hull_size));

  // invader is teleported up in the sky to land on our heads
     ship.altitude = app_height - ship.hull_size / 2;
     if (radar_detect_collision(i)) {
        ship.status = 0; // teleport failure, radar detected an enevitable collision
        return; // just abandon this teleport, wait for next
     }
   // paint the enemy
     ship.war_paint.red   = rnd.next(0.5) + 0.5; // we want random color, but not too dark
     ship.war_paint.green = rnd.next(0.5) + 0.5; // because the background is black
     ship.war_paint.blue  = rnd.next(0.5) + 0.5;
     ship.war_paint.alpha = rnd.next(0.5) + 0.5; // slightly transparent

     ship.status = 1; // active
     attack++;
     break;
  }
}

// score for hitting the small ship is bigger
int value_of_ship(int i) {
  double coeff = max_hull_size / (max_hull_size - min_hull_size);
  int range = max_hull_size - min_hull_size;
  int X = max_hull_size - enemies[i].hull_size;
  X = X * X / range * coeff;
  if (X < 1) return 1;
  // smaller ship will cost 100, bigger 1
  return X;
}

void begin_gl() {
   // clear screen with black
   glClearColor(0.0,0.0,0.0,0.0);
   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
   // remove any transformation matrix (if defined)
   glLoadIdentity();
}

void hologram(double x, double y, double R, color C) {
   // draw a circle using OpenGL single round point
   // OpenGL has no builtin circle function, but it allows smooth round points
   glPointSize(R);
   glBegin(GL_POINTS);
   glColor4f(C.red, C.green, C.blue, C.alpha);
   glVertex2f(x, app_height - y);
   glEnd();
}

void end_gl() {
   // yes, use double buffer
   SwapBuffers(global_hdc);
}

// small numeric "font"

char *digit_bitmap =
" o   o   o  oo  o o ooo ooo ooo  o  ooo "
"o o oo  o o   o o o o   o     o o o o o "
"ooo  o    o  o  ooo oo  ooo   o  o  ooo "
"o o  o   o    o   o   o o o  o  o o   o "
" o   o  ooo oo    o oo  ooo  o   o  oo  "
"                                        ";

// print single number using the "font" above
// for this task using textures or Windows TextOut is not wise
// 'pixel' size is 10, character placement is 4x6 including one spacing pixel on the right

void print_number(int x, int y, int number) {
   glPointSize(10);
   while (true) {
      int digit = number % 10; // similar to "convert int to string"
      for (int c = 0; c < 4; c++) {
         for (int r = 0; r < 6; r++) {
            int n = c + r * 40 + digit * 4;
            bool draw = digit_bitmap[n] == 'o';
            if (draw) {
               glBegin(GL_POINTS);
               glColor4f(1, 1, 1, 0.5);
               glVertex2f(x + c * 10, y + r * 10);
               glEnd();
            }
         }
      }
      number /= 10;
      x -= 40;
      if (number == 0) break;
   }
}

// main physics and drawing routine step()
// it will calculate the new positions of each invader and call hologram() to show them
// also will draw scores

void step(int time_elapsed) {
  begin_gl();
  for (int i = 0; i < maximum_attack; i++) {
     invader &ship = enemies[i];
     if (ship.status > 0) {
        double max_speed = 0.2, min_speed = 0.05;
        double x = ship.position, y = ship.altitude;
        double R = ship.hull_size / 2;
        // calculate speed value, smaller ships fall faster
        // speed is constant, no gravity here
        double Q = (max_speed - min_speed) / (max_hull_size - min_hull_size);
        double speed = max_hull_size - (ship.hull_size - min_hull_size);
        speed = min_speed + speed * Q;
        ship.altitude -= speed * time_elapsed * attack_speed;
        if (ship.altitude < 0) ship.status = 0, attack--;
        hologram(x, y, R*2, ship.war_paint); // hologram aka set_pixel() aka draw_circle()
     }
  }
  // draw cursor/haircross
  glPointSize(10);
  glBegin(GL_POINTS);
  glColor4f(1, 1, 1, 1);
  glVertex2f(cursor_x, app_height - cursor_y);
  glEnd();
  // show scores in the lower left corner
  print_number(10 * 4 * 4, app_height - 60, scores);
  end_gl(); // swap frames
}

// handle the mouse click
void on_click() {
  int x = cursor_x, y = cursor_y;
  int hit = -1;
  // find if every active ship collides with the mouse pointer
  for (int i = 0; i < maximum_attack; i++) {
    invader &b = enemies[i];
    if (b.status == 0) continue;
    double X = (x - b.position), Y = (y) - b.altitude;
    double distance = sqrt(X * X + Y * Y);
    // assume mouse pointer diameter = 3
    if (distance < (3 + b.hull_size) / 2) { hit = i; break; }
  }
  // kill the hit ship
  if (hit >= 0) enemies[hit].status = 0, attack--, scores += value_of_ship(hit);
  scores %= 10000; // 9999 should be enough for anyone
}
  
// window message handler
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    HDC         hdc = NULL;
    HGLRC       hrc = NULL;
    PAINTSTRUCT ps;
    switch (msg) {
        case WM_CREATE: {
            // Setup OpenGL
            hdc = BeginPaint(hwnd, &ps);
            global_hdc = hdc;
            // This is standard OpenGL creation routine
            SetupPixelFormat(hdc);
            hrc = wglCreateContext(hdc);
            wglMakeCurrent(hdc, hrc);
            // Disable V-Sync if OpenGL SwapInterval extension is supported
            PFNWGLSWAPINTERVALEXTPROC       wglSwapIntervalEXT = NULL;
            if (WGLExtensionSupported("WGL_EXT_swap_control")) {
                wglSwapIntervalEXT = (PFNWGLSWAPINTERVALEXTPROC) wglGetProcAddress("wglSwapIntervalEXT");
                wglSwapIntervalEXT(0); // This line disables V-Sync
            }
            // By default OpenGL points are squares, we need them round
            glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);
            glEnable(GL_POINT_SMOOTH);
            // Enable transparency
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glEnable(GL_BLEND);
            break; }
        case WM_SIZE: {
            // set OpenGL 2D mode according to the window size
            // this mode is not compatible with 3D
            int w = app_width, h = app_height;
            glViewport(0, 0, w, h);
            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            glOrtho(0, w, h, 0, -1, 1);
            glMatrixMode (GL_MODELVIEW);
            break; }
            
        case WM_CLOSE: // clean up and quit
            // standard windows/opengl stuff here
            wglMakeCurrent(hdc, NULL);
            wglDeleteContext(hrc);
            EndPaint(hwnd, &ps);
            PostQuitMessage(0);
            DestroyWindow(hwnd);
            quit = true; // global app quit variable
            return 0;
        case WM_TIMER:
            // add new ship if we have free slots
            // this timer is set to 0.5 seconds, see SetTimer() call below
            if (attack < maximum_attack) teleport_invader();
            break;
        case WM_LBUTTONDOWN: // only handle left mouse button down
            on_click();
            SetCursor(0); // remove windows cursor
            break;
        case WM_LBUTTONUP:
            SetCursor(0); // remove windows cursor
            break;
        case WM_MOUSEMOVE: {
            // windows coordinate origin is top left corner
            // opengl origin is bottom left!
            // and calculation of window non-client area (titlebar) must be considered
            cursor_x = LOWORD(lParam);
            cursor_y = app_height - HIWORD(lParam);
            RECT R = {0, 0, app_width, app_height};
            AdjustWindowRect(&R, GetWindowLong(hwnd, GWL_STYLE), false);
            cursor_y += R.top; // adjust title bar height
            SetCursor(0); // remove windows cursor
            }
            break;
        case WM_KEYDOWN:
            quit = true; // close if any key is pressed
            break;
        default:
            // standard WinAPI stuff
            return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int iCmdShow) {
//int main() {
    // WinAPI standard window creation, register class, create window, show window
    HWND     hwnd;
    MSG      msg;
    //HINSTANCE hInstance = 0; int iCmdShow = SW_SHOW;
    WNDCLASS wndclass = {0};
    wndclass.lpfnWndProc   = WndProc;
    wndclass.hInstance     = hInstance;
    wndclass.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    wndclass.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wndclass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wndclass.lpszMenuName  = NULL;
    wndclass.lpszClassName = L"GameClass";
    RegisterClass(&wndclass);
    hwnd = CreateWindow(L"GameClass", szTitle, WS_BORDER|WS_SYSMENU,
                        0, 0, app_width, app_height, NULL, NULL, hInstance, NULL);
    ShowWindow(hwnd, iCmdShow);
    UpdateWindow(hwnd);

    // set "slow" timer that will handle teleportation of new enemy ships on the screen
    SetTimer(hwnd, 1, 500, 0);
    // Start the game loop, this is the normal way to handle responsive FPS apps in Windows
    while(!quit) {
      if (PeekMessage(&msg, hwnd, 0, 0, PM_REMOVE)) {
         TranslateMessage(&msg);
         DispatchMessage(&msg);
      } else {
         static int interval = 0;
         int T = time1000();
         step(time1000() - interval); // call the main routine, and tell it
                                      // how much time elapsed since it's last invocation
                                      // so that it can do correct physics
                                      // this allows very smooth animation and responsive UI
                                      // this game needs very precise animation and
                                      // synchronized mouse handling as well
         interval = T;
         Sleep(1); // wait at least 1 ms
      }
    }

    return msg.wParam;
}
