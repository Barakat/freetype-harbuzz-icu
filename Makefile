
all:
	g++ -std=c++11 -Wall -Wextra -pedantic freetype-harbuzz-icu.cpp `pkg-config.exe --cflags --libs freetype2 harfbuzz icu-uc` -o render


