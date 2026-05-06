#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_keycode.h>
#include <SDL2/SDL_timer.h>
#include <SDL2/SDL_video.h>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>
#include <math.h>
#include <mutex>
#include <stdio.h>
#include <string>
#include <thread>
#include <vector>

using namespace std;

const int workers_count = thread::hardware_concurrency();
const int particles_count = 1000000 / workers_count;
const int total_particles = particles_count * workers_count;
const int FPS = 120;
const int frame_delay = 1000 / FPS;
bool wrap = false;
int screen_width = 1920;
int screen_height = 1000;

bool mouse_down = false;
bool pause = false;

struct Point2 {
  float x, y;
  Point2(float x = 0, float y = 0) : x{x}, y{y} {}
};

void parallel_update(SDL_FPoint *positions, SDL_FPoint *velocities, int count,
                     Point2 vortex, float dt, float damping_base,
                     float orbital_force, float attract_strength) {
  float s = dt * 60.0f;
  float damping = powf(damping_base, s);

  for (int i = 0; i < count; i++) {
    float dx = vortex.x - positions[i].x;
    float dy = vortex.y - positions[i].y;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist < 1.0f)
      dist = 1.0f;
    float inv_dist = 1.0f / dist;

    float ux = dx * inv_dist;
    float uy = dy * inv_dist;

    float ax, ay;
    if (mouse_down) {
      float k = attract_strength * (1.0f + orbital_force * inv_dist) * s;
      ax = ux * k;
      ay = uy * k;
    } else {
      float t = orbital_force * inv_dist * s;
      ax = ux * s - uy * t;
      ay = uy * s + ux * t;
    }

    velocities[i].x = (velocities[i].x + ax) * damping;
    velocities[i].y = (velocities[i].y + ay) * damping;
    positions[i].x += velocities[i].x * s;
    positions[i].y += velocities[i].y * s;

    if (wrap) {
      positions[i].x = fmodf(fmodf(positions[i].x, screen_width) + screen_width,
                             screen_width);
      positions[i].y = fmodf(
          fmodf(positions[i].y, screen_height) + screen_height, screen_height);
    }
  }
}

struct ThreadPool {
  vector<thread> threads;

  mutex work_mtx;
  condition_variable work_cv;
  int generation = 0;
  bool quit_flag = false;
  Point2 vortex_shared;
  float dt_shared = 0.0f;
  float damping_shared = 0.998f;
  float orbital_shared = 10.0f;
  float attract_shared = 3.0f;

  mutex done_mtx;
  condition_variable done_cv;
  atomic<int> done_count{0};

  SDL_FPoint *pos_data;
  SDL_FPoint *vel_data;

  void worker(int idx) {
    int seen_gen = 0;
    while (true) {
      Point2 v{0, 0};
      {
        unique_lock<mutex> lock(work_mtx);
        work_cv.wait(lock, [&] { return generation != seen_gen || quit_flag; });
        if (quit_flag)
          return;
        seen_gen = generation;
        v = vortex_shared;
      }

      parallel_update(pos_data + idx * particles_count,
                      vel_data + idx * particles_count, particles_count, v,
                      dt_shared, damping_shared, orbital_shared,
                      attract_shared);

      if (done_count.fetch_add(1) + 1 == (int)threads.size()) {
        lock_guard<mutex> lock(done_mtx);
        done_cv.notify_one();
      }
    }
  }

  void init(SDL_FPoint *pos, SDL_FPoint *vel) {
    pos_data = pos;
    vel_data = vel;
    for (int i = 0; i < workers_count; i++)
      threads.emplace_back(&ThreadPool::worker, this, i);
  }

  void run(Point2 v, float dt, float damping, float orbital, float attract) {
    {
      lock_guard<mutex> lock(work_mtx);
      vortex_shared = v;
      dt_shared = dt;
      damping_shared = damping;
      orbital_shared = orbital;
      attract_shared = attract;
      done_count.store(0);
      generation++;
    }
    work_cv.notify_all();

    unique_lock<mutex> lock(done_mtx);
    done_cv.wait(lock,
                 [&] { return done_count.load() == (int)threads.size(); });
  }

  void stop() {
    {
      lock_guard<mutex> lock(work_mtx);
      quit_flag = true;
    }
    work_cv.notify_all();
    for (auto &t : threads)
      t.join();
  }
};

void print_usage_and_exit() {
  printf(
      "usage: particle-simulator [option] [width height]\n\n"
      "options:\n"
      "  -h, --help          show this help message and exit\n\n"
      "arguments:\n"
      "  width  height       specify the screen width and height. optional.\n\n"
      "examples:\n"
      "  particle-simulator -h\n"
      "  particle-simulator 1920 1080\n\n");
  exit(1);
}

void arg_error() {
  fprintf(stderr, "error: bogus arguments\n");
  print_usage_and_exit();
}

const char *vert_src = R"glsl(
#version 330 core
layout(location = 0) in vec2 pos;
uniform vec2 screen;
void main() {
    vec2 ndc = (pos / screen) * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, -ndc.y, 0.0, 1.0);
}
)glsl";

const char *frag_src = R"glsl(
#version 330 core
out vec4 color;
void main() {
    color = vec4(1.0);
}
)glsl";

GLuint compile_shader(GLenum type, const char *src) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);
  return s;
}

int main(int argc, char **argv) {
  srand(1);

  if (argc > 1) {
    auto argv1 = string(argv[1]);

    if (argv1 == "--help" || argv1 == "-h")
      print_usage_and_exit();

    if (argc <= 2)
      arg_error();

    if ((screen_width = atoi(argv[1])) == 0)
      arg_error();

    if ((screen_height = atoi(argv[2])) == 0)
      arg_error();
  }

  printf("target FPS: %d\n", FPS);
  printf("threads: %d\n", workers_count);
  if (workers_count == 0)
    return 1;
  printf("total particles: %d\n", total_particles);
  printf("\n\n");

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
    return 1;
  }

  SDL_Window *window = SDL_CreateWindow(
      "particle-simulator", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
      screen_width, screen_height, SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL);

  if (!window) {
    printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
    return 1;
  }

  SDL_GLContext gl_ctx = SDL_GL_CreateContext(window);
  if (!gl_ctx) {
    fprintf(stderr, "could not create GL context: %s\n", SDL_GetError());
    return 1;
  }
  SDL_GL_SetSwapInterval(0);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();
  ImGui_ImplSDL2_InitForOpenGL(window, gl_ctx);
  ImGui_ImplOpenGL3_Init("#version 330 core");

  GLuint vert = compile_shader(GL_VERTEX_SHADER, vert_src);
  GLuint frag = compile_shader(GL_FRAGMENT_SHADER, frag_src);
  GLuint program = glCreateProgram();
  glAttachShader(program, vert);
  glAttachShader(program, frag);
  glLinkProgram(program);
  glDeleteShader(vert);
  glDeleteShader(frag);
  glUseProgram(program);
  glUniform2f(glGetUniformLocation(program, "screen"), screen_width,
              screen_height);

  GLuint vao, vbo;
  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(SDL_FPoint), nullptr);
  glEnableVertexAttribArray(0);

  auto flat_pos = vector<SDL_FPoint>(total_particles);
  auto flat_vel = vector<SDL_FPoint>(total_particles);

  for (int i = 0; i < total_particles; i++) {
    flat_pos[i] = {(float)(rand() % screen_width),
                   (float)(rand() % screen_height)};
    flat_vel[i] = {0, 0};
  }

  ThreadPool pool;
  pool.init(flat_pos.data(), flat_vel.data());

  float damping_base = 0.998f;
  float orbital_force = 10.0f;
  float attract_strength = 3.0f;

  Point2 vortex = Point2((float)screen_width / 2, (float)screen_height / 2);
  bool quit = false;
  uint32_t prev_ticks = SDL_GetTicks();

  while (!quit) {

    uint32_t frame_start = SDL_GetTicks();
    float dt = (frame_start - prev_ticks) / 1000.0f;
    prev_ticks = frame_start;
    if (dt > 0.05f)
      dt = 0.05f;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    ImGuiIO &io = ImGui::GetIO();

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL2_ProcessEvent(&event);
      switch (event.type) {
      case SDL_QUIT:
        quit = true;
        break;
      case SDL_MOUSEBUTTONDOWN:
        if (!io.WantCaptureMouse && event.button.button == SDL_BUTTON_LEFT)
          mouse_down = true;
        break;
      case SDL_MOUSEMOTION:
        if (!io.WantCaptureMouse)
          vortex = Point2(event.motion.x, event.motion.y);
        break;
      case SDL_MOUSEBUTTONUP:
        mouse_down = false;
        break;
      case SDL_KEYDOWN:
        if (!io.WantCaptureKeyboard && event.key.keysym.sym == SDLK_SPACE)
          pause = !pause;
        break;
      default:
        break;
      }
    }

    ImGui::Begin("Controls");
    ImGui::SliderFloat("Damping", &damping_base, 0.990f, 1.000f, "%.4f");
    ImGui::SliderFloat("Orbital force", &orbital_force, 0.0f, 50.0f);
    ImGui::SliderFloat("Attract strength", &attract_strength, 0.0f, 10.0f);
    ImGui::Checkbox("Wrap", &wrap);
    ImGui::Text("%.1f FPS", io.Framerate);
    ImGui::End();

    if (!pause)
      pool.run(vortex, dt, damping_base, orbital_force, attract_strength);

    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(program);
    glBindVertexArray(vao);
    glBufferData(GL_ARRAY_BUFFER, total_particles * sizeof(SDL_FPoint),
                 flat_pos.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_POINTS, 0, total_particles);

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    SDL_GL_SwapWindow(window);

    uint32_t frame_time = SDL_GetTicks() - frame_start;
    if (frame_time < frame_delay)
      SDL_Delay(frame_delay - frame_time);
    fprintf(stderr, "\rrender time: %dms (%f FPS)   ", frame_time,
            1 / (float)frame_time * 1000);
    fflush(stderr);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
  pool.stop();
  glDeleteBuffers(1, &vbo);
  glDeleteVertexArrays(1, &vao);
  glDeleteProgram(program);
  SDL_GL_DeleteContext(gl_ctx);
  SDL_DestroyWindow(window);
  SDL_Quit();
}
