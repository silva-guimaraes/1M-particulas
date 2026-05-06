CXX      = g++
TARGET   = particle-simulator
CXXFLAGS = -std=c++17 -I backends -I /usr/include/SDL2 -O3 -march=native -ffast-math -MMD -MP
LDFLAGS  = -lSDL2 -lGL

SRCS = main.cpp \
       backends/imgui.cpp \
       backends/imgui_draw.cpp \
       backends/imgui_tables.cpp \
       backends/imgui_widgets.cpp \
       backends/imgui_impl_sdl2.cpp \
       backends/imgui_impl_opengl3.cpp

OBJS = $(SRCS:.cpp=.o)
DEPS = $(OBJS:.o=.d)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(DEPS) $(TARGET)

-include $(DEPS)

.PHONY: clean
