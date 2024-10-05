#CC= gcc
CC= arm-linux-gnueabihf-gcc
INC_DIRS= -I/home/chrisk2002/openssl-1.1.1t/openssl-arm/include -I/home/chrisk2002/libwebsockets/include -I/home/chrisk2002/libwebsockets/build 
LIB_DIRS= -L/home/chrisk2002/openssl-1.1.1t/openssl-arm/lib

CFLAGS= -Wall -g $(INC_DIRS)
LDFLAGS= $(LIB_DIRS) -lwebsockets -pthread -lssl -lcrypto -ljansson

SRC= /home/chrisk2002/project_pitsianis/final.c
TARGET= final


all: $(TARGET)


$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGET)
