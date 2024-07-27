#include <SDL2/SDL_error.h>
#include <SDL2/SDL_events.h>
#include <SDL2/SDL_keycode.h>
#include <SDL2/SDL_mouse.h>
#include <SDL2/SDL_render.h>
#include <SDL2/SDL_stdinc.h>
#include <SDL2/SDL_surface.h>
#include <SDL2/SDL_thread.h>
#include <SDL2/SDL_timer.h>
#include <SDL2/SDL_video.h>
#include <cmath>
#include <cstdlib>
// #include <functional>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <SDL2/SDL.h>
#include <stdbool.h>
#include <vector>
#include <thread>


const int workers_count = 12;
const int particles_count = 1000000 / workers_count;
const int SCREEN_WIDTH = 1920;
const int SCREEN_HEIGHT = 1000;
const int FPS = 120;
const int frame_delay = 1000 / FPS;
const bool wrap = false;

SDL_Renderer* renderer;
bool mouse_down = false;
bool pause = true;


class Point2 {

public:
    float x, y;

    Point2(float x = 0, float y = 0) : x{x}, y{y} { }

    void draw() {

        auto nx = (int) x;
        auto ny = (int) y;

        if (wrap) {
            nx = ((int)x % SCREEN_WIDTH + SCREEN_WIDTH) % SCREEN_WIDTH;
            ny = ((int)y % SCREEN_HEIGHT + SCREEN_HEIGHT) % SCREEN_HEIGHT;
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

class Dot {
public:
    Point2 pos, vel;

    void update() {
        pos += vel;
        vel *= .998;
    }

    void draw() {
        pos.draw();
    }

    Dot(Point2 p) : pos{p} { }
};


void parallel_update(std::vector<Dot>& points, Point2 vortex) {
    auto p = points.data();

    for (int i = 0; i < particles_count; i++) {

        auto vortex_direction = (vortex - p[i].pos).unit();
        auto vortex_distance = p[i].pos.distance(vortex);
        auto vortex_rotation = vortex_direction.rotate(100) * (1 / vortex_distance * 10);

        p[i].vel += vortex_direction + vortex_rotation;

        p[i].update();
    }
}




int main()
{
    srand(1);

    if( SDL_Init( SDL_INIT_VIDEO ) < 0 ) {
        printf( "SDL could not initialize! SDL_Error: %s\n", SDL_GetError() );
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow( "SDL Tutorial",
                               SDL_WINDOWPOS_UNDEFINED,
                               SDL_WINDOWPOS_UNDEFINED,
                               SCREEN_WIDTH,
                               SCREEN_HEIGHT,
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
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
    };

    auto workers = std::vector<std::thread>();
    auto workers_points = std::vector<std::vector<Dot>>();

    for (int i = 0; i < workers_count; i++) {

        auto points = std::vector<Dot>();

        for (int i = 0; i < particles_count; i++) {
            auto random_pos = Point2(rand() % SCREEN_WIDTH, rand() % SCREEN_HEIGHT);
            auto random_direction = Point2(rand_range(1), rand_range(1));

            auto dot = Dot(random_pos);
            dot.vel = random_direction;

            points.push_back(dot);
        }


        workers_points.push_back(points);
    }

    Point2 vortex = Point2((float) SCREEN_WIDTH / 2, (float) SCREEN_HEIGHT / 2);
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

            std::thread t(parallel_update, std::ref(workers_points[i]), vortex);
            workers.push_back(std::move(t));
        }
        for (auto worker = workers.begin(); worker != workers.end(); ++worker) {
            worker->join();
        }

        workers.clear();
        for (int i = 0; i < workers_count; i++)
            for (int j = 0; j < particles_count; j++)
                workers_points.at(i).at(j).draw();


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
