CC = gcc
CFLAGS = -Wall -g
LIBS = -lpthread -lrt

BIN_DIR = bin
LIB_DIR = lib
OBJ_DIR = obj

$(shell mkdir -p $(BIN_DIR) $(LIB_DIR) $(OBJ_DIR))

LIBRARY = $(LIB_DIR)/libksocket.a
LIBRARY_OBJS = $(OBJ_DIR)/ksocket.o

INIT_EXEC = $(BIN_DIR)/initksocket
USER1_EXEC = $(BIN_DIR)/user1
USER2_EXEC = $(BIN_DIR)/user2

all: $(LIBRARY) $(INIT_EXEC) $(USER1_EXEC) $(USER2_EXEC)

$(OBJ_DIR)/%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(LIBRARY): $(LIBRARY_OBJS)
	ar rcs $@ $^

$(INIT_EXEC): initksocket.c $(LIBRARY)
	$(CC) $(CFLAGS) $< -o $@ -L$(LIB_DIR) -lksocket $(LIBS)

$(USER1_EXEC): user1.c $(LIBRARY)
	$(CC) $(CFLAGS) $< -o $@ -L$(LIB_DIR) -lksocket $(LIBS)

$(USER2_EXEC): user2.c $(LIBRARY)
	$(CC) $(CFLAGS) $< -o $@ -L$(LIB_DIR) -lksocket $(LIBS)

clean:
	rm -rf $(OBJ_DIR)/* $(LIB_DIR)/* $(BIN_DIR)/*

.PHONY: all clean