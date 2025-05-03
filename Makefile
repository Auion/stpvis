RAYLIB_OBJ = raylib/raylib/libraylib.a 
#SELF_OBJS = chunks.o render.o

ASAN ?= -fsanitize=address

stp: main.c $(SELF_OBJS)
	cc -O3 -Wall -Wextra -Wl,-z -Wl,stack-size=536870912 main.c -g -lGL -lGLEW -lglut -lm $(RAYLIB_OBJ) $(SELF_OBJS) -o stp $(ASAN)

# chunks.o: chunks.c
# 	cc -O3 -Wall -Wextra -g -c chunks.c -o chunks.o
#
# render.o: render.c
# 	cc -O3 -Wall -Wextra -g -c render.c -o render.o
