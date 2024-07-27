#include <SDL2/SDL_error.h>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_keycode.h>
#include <SDL2/SDL_mouse.h>
#include <SDL2/SDL_rect.h>
#include <SDL2/SDL_render.h>
#include <SDL2/SDL_stdinc.h>
#include <SDL2/SDL_surface.h>
#include <SDL2/SDL_thread.h>
#include <SDL2/SDL_timer.h>
#include <SDL2/SDL_video.h>
#include <cmath>
#include <complex>
#include <cstdlib>
// #include <functional>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <SDL2/SDL.h>
#include <stdbool.h>
#include <string>
#include <vector>
#include <thread>


using namespace std;


const int workers_count = thread::hardware_concurrency();
const int particles_count = 1000000 / workers_count;
const int FPS = 120;
const int frame_delay = 1000 / FPS;
const bool wrap = false;
int screen_width = 1920;
int screen_height = 1000;

SDL_Renderer* renderer;
bool mouse_down = false;
bool pause = false;


class Point2 {

public:
    float x, y;

    Point2(float x = 0, float y = 0) : x{x}, y{y} { }

    void draw() {

        auto nx = (int) x;
        auto ny = (int) y;

        if (wrap) {
            nx = ((int)x % screen_width + screen_width) % screen_width;
            ny = ((int)y % screen_height + screen_height) % screen_height;
        }


        SDL_SetRenderDrawColor(renderer, 0xff, 0xff, 0xff, 0xff);

        SDL_RenderDrawPointF(renderer, nx, ny);
    }

    inline Point2 operator+(Point2 p) {
        return Point2(x + p.x, y + p.y);
    }

    inline Point2 operator-(Point2 p) {
        return Point2(x - p.x, y - p.y);
    }

    inline Point2 operator*(float s) {
        return Point2(x * s, y * s);
    }

    inline Point2 operator*(Point2 b) {
        return Point2(x * b.x, y * b.y);
    }

    inline void operator*=(float s) {
        *this = (*this * s);
    }

    inline void operator+=(Point2 p) {
        x += p.x;
        y += p.y;
    }

    inline double mag() {
        return std::sqrt(std::pow(x, 2) + std::pow(y, 2));
    }

    inline Point2 unit() {
        auto m = mag();
        return Point2(x / m, y / m);
    }

    inline double distance(Point2 b) {
        return (*this - b).mag();
    }

    inline Point2 rotate(float angle) {
        return Point2(
            x * cos(angle) - y * sin(angle),
            x * sin(angle) + y * cos(angle)
        );
    }

};

float rand_range(float range) {
    return rand() / (float) RAND_MAX * range - (range / 2);
}

void parallel_update(vector<SDL_FPoint>& positions, vector<SDL_FPoint>& velocities, Point2 vortex) {

    for (int i = 0; i < particles_count; i++) {
        auto pos = Point2(positions[i].x, positions[i].y);

        auto vortex_direction = (vortex - pos).unit();
        auto vortex_distance = pos.distance(vortex);
        auto vortex_rotation = vortex_direction.rotate(100) * (1 / vortex_distance * 10);

        auto vel = (vortex_direction + vortex_rotation) * (mouse_down ? 3 : 1);
        velocities[i].x += vel.x;
        velocities[i].y += vel.y;

        positions[i].x += velocities[i].x;
        positions[i].y += velocities[i].y;

        velocities[i].x *= .998;
        velocities[i].y *= .998;
    }
}


void print_usage_and_exit() {
    fprintf(stderr, "usage: particle-simulator [screen_width screen_height]\n");
    exit(1);
}


int main(int argc, char** argv)
{
    srand(1);

    if (argc == 3) {
        if (string(argv[1]) == "--help" || string(argv[1]) == "-h")
            print_usage_and_exit();
        
        if ((screen_width = atoi(argv[1])) == 0 || (screen_height = atoi(argv[2])) == 0) {
            fprintf(stderr, "error: bogus arguments\n");
            print_usage_and_exit();
        }
    }


    printf("target FPS: %d\n", FPS);
    printf("threads: %d\n", workers_count);
    if (workers_count == 0)
        return 1;
    printf("total particles: %d\n", particles_count * workers_count);
    printf("\n");
    printf("\n");







    if( SDL_Init( SDL_INIT_VIDEO ) < 0 ) {
        printf( "SDL could not initialize! SDL_Error: %s\n", SDL_GetError() );
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow( "SDL Tutorial",
                               SDL_WINDOWPOS_UNDEFINED,
                               SDL_WINDOWPOS_UNDEFINED,
                               screen_width,
                               screen_height,
                               SDL_WINDOW_SHOWN
                               );

    if(!window) {
        printf( "Window could not be created! SDL_Error: %s\n", SDL_GetError() );
        return 1;
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        fprintf(stderr, "could not create renderer: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Rect fill_rect = {
        0,
        0,
        screen_width,
        screen_height,
    };

    auto workers = vector<thread>();
    auto points_pos = vector<vector<SDL_FPoint>>();
    auto points_vel = vector<vector<SDL_FPoint>>();

    for (int i = 0; i < workers_count; i++) {

        auto pos = vector<SDL_FPoint>();
        auto vel = vector<SDL_FPoint>();

        for (int i = 0; i < particles_count; i++) {

            pos.push_back({ (float) (rand() % screen_width), (float) (rand() % screen_height), });

            vel.push_back({ 0, 0 });
        }

        points_pos.push_back(pos);
        points_vel.push_back(vel);
    }


    Point2 vortex = Point2((float) screen_width / 2, (float) screen_height / 2);
    bool quit = false;

    while (!quit) {

        uint32_t frame_start = SDL_GetTicks();

        SDL_Event event;

        while (SDL_PollEvent(&event))
        {
            switch (event.type) {
                case SDL_QUIT:
                    quit = true;
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    if (event.button.button != SDL_BUTTON_LEFT)
                        break;

                    mouse_down = true;
                    break;
                case SDL_MOUSEMOTION:
                    vortex = Point2(event.motion.x, event.motion.y);
                    break;
                case SDL_MOUSEBUTTONUP:
                    mouse_down = false;
                    break;
                case SDL_KEYDOWN:
                    if (event.key.keysym.sym == SDLK_SPACE)
                        pause = !pause;

                    break;
                default:
                    break;
            }
        }


        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        SDL_RenderClear(renderer);

        for (int i = 0 ; i < workers_count; i++) {

            if (pause)
                continue;

            thread t(parallel_update, ref(points_pos[i]), ref(points_vel[i]), vortex);
            workers.push_back(std::move(t));
        }

        for (auto& worker : workers) {
            if (worker.joinable())
                worker.join();
        }

        workers.clear();

        SDL_SetRenderDrawColor(renderer, 0xff, 0xff, 0xff, 0xff);
        for (int i = 0; i < workers_count; i++)
             SDL_RenderDrawPointsF(renderer, points_pos[i].data(), particles_count);

        SDL_RenderPresent(renderer);

        uint32_t frame_time = SDL_GetTicks() - frame_start;

        if (frame_time < frame_delay) {
            SDL_Delay(frame_delay - frame_time);
        }
        fprintf(stderr, "\rrender time: %dms (%f FPS)   ", frame_time, 1 / (float) frame_time * 1000);
        fflush(stderr);

    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
